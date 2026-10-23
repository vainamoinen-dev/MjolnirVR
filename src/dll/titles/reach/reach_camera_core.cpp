// reach_camera_core.cpp: Halo Reach's camera core and everything built on it (moved from game.cpp in E2,
// unchanged): the stereo transaction, main and player views, first-person interpolation, palette and
// camera rebuild, vehicles, visual hands, aim feedback, HUD/CHUD, muzzle, weather and render controls,
// install and retirement. Core reaches it through reach_bridge.h and it reaches core through
// game_internal.h; the Reach .inl files are included at the same points, in the anonymous namespace.
#include "../../core/game_internal.h"
#include "reach_bridge.h"
#include "../../core/guard_count.h"
using namespace game_core;

namespace
{
    // Inside the anonymous namespace, so the bridge wrappers at the end can name the .inl functions as ::Name.
    using namespace reach_bridge;
} // namespace

namespace
{
    // Matches kReachCapabilities in title_registry.cpp. Hud is granted: Reach's
    // own curvature record is located by kReachHudLayoutAdapter, so hud_size
    // and hud_aspect drive its native layout exactly as they do for Halo 3.
    // ArmIk is deliberately NOT granted. Reach reported no capabilities at all
    // until PublishReachLifecycle existed, so every arm-gated capability turned
    // on at once the first time it did. ControllerAim was the one the VR
    // crosshair needed and it is headset-confirmed working; ArmIk immediately
    // attached the left hand to the player's face, because Reach's arm IK is
    // not solved yet. Grant it only once that is proven in the headset.
    constexpr uint32_t kReachRuntimeCapabilities = TitleCapability_Stereo | TitleCapability_ControllerAim |
                                                   TitleCapability_Hud | TitleCapability_RuntimeModes |
                                                   TitleCapability_RoomScale | TitleCapability_ControllerInput |
                                                   TitleCapability_Haptics | TitleCapability_CutsceneTheater;
} // namespace
namespace reach_bridge
{
    constinit ShotHeadPose g_reachShotHead;
    // Reach has no controller-aim body-heading contract yet, so its stock
    // movement heading is the yaw of the pristine per-frame compact camera (the
    // engine's own facing), published from the Reach visibility build. Head-
    // relative locomotion rotates the move stick by (gaze - this) so forward
    // walks where the headset looks. This is the Reach analog of g_aimFwd; it is
    // NOT published as aim and writes nothing back into the game.
    constinit std::atomic<float> g_reachStockHeadingYaw{0.0f};
    constinit std::atomic<bool> g_reachMoveHeadingValid{false};
    bool ReachFollowContextValid(uint32_t& outGeneration);
} // namespace reach_bridge
namespace
{
    constinit weapon_muzzle::Store g_reachBarrelMuzzles;
    constinit std::atomic<bool> g_reachBarrelBindingsReady{false};
    bool ReachCaptureMuzzleOwner(int view, int slot, uint32_t& unit, uint32_t& weapon) noexcept;
    void ReachApplyVisualHandOffsets(
        uint16_t tag, const int32_t* boneMap, const ReachFpInterpolationContext& context, BoneMatrix* destination);
    void ReachPublishMuzzlePalette(
        uint16_t tag, const ReachFpInterpolationContext& context, const BoneMatrix* destination) noexcept;
    void ReachMuzzleTrace(const char* stage, uint16_t tag, uint32_t a, uint32_t b, uint32_t c) noexcept;
    bool InstallReachMuzzle(uintptr_t base, size_t size, uint32_t generation);
    bool InstallReachWeaponHaptics(uintptr_t base, size_t size, uint32_t generation);
    bool RemoveReachWeaponHaptics();
    void ReportReachWeaponHaptics();
    bool RemoveReachMuzzle();
    void ReportReachMuzzle();
    bool InstallReachGrabPickup(uintptr_t base, size_t size, uint32_t generation);
    bool RemoveReachGrabPickup();
    void ReportReachGrabPickup();
    void ReachGrabPublishHead(const float q[4]);
} // namespace
namespace reach_bridge
{
    bool ReachPrimaryWeaponEvidence(uint32_t expectedGeneration, uint32_t& unitOut, uint32_t& weaponOut,
        uint32_t& detailOut, bool* primaryAbsentOut) noexcept;
    bool DiagnosticReachPrimary(
        uint32_t expectedGeneration, uint32_t& unitOut, uint32_t& weaponOut, uint32_t& detailOut) noexcept;
} // namespace reach_bridge
namespace
{
    bool ReachApplyBarrelAim(
        int32_t unit, float* origin, float* direction, const float* velocity, uint8_t collision, uint32_t simulation);
    // T-2 shot evidence: the firing context of the Reach projectile transaction
    // currently on this thread (weapon datum handle + barrel), read from the
    // muzzle feature's per-thread request. UINT32_MAX / kTelemetryShotIndexUnknown
    // when the optional muzzle transaction is absent. Defined with that feature.
    void ReachReadFiringContextForTelemetry(uint32_t& weapon, uint8_t& barrel);
    bool ReachContactMeleeReady();
    void ReportReachContactMelee();
    void ReachPublishContactHand(int hand, const FpExplicitPoseTargets& targets, const FpInterpolationContext& context,
        const BoneMatrix& root, const BoneMatrix* solved, const float points[][3], int count, int32_t unit,
        uint32_t generation);
    bool ReachRedirectContactVector(uintptr_t caller, uint64_t flags, int32_t mode, int32_t ignoredA, int32_t ignoredB,
        int32_t ignoredC, void* result, uint8_t& returned);
} // namespace
namespace reach_bridge
{
    // Reach has no camera-copy hook to beat like Halo 3 and ODST. Its armed
    // per-eye core is the equivalent liveness proof, so the Present-thread
    // Reach branch stamps this beacon while that core owns the frame.
    constinit std::atomic<uint64_t> g_reachLastCamCopyMs{0};
    // Defined after g_reachCamera; its install-scoped generation is the Reach
    // parallel of g_halo3RuntimeGeneration / g_odstRuntimeGeneration.
    uint32_t ReachRetainedRuntimeGeneration();
} // namespace reach_bridge
namespace
{
    // Reach sensor origin, as authored, per layout slot (see ReachShiftSensorOrigin). A slot whose live
    // origin equals what that function last wrote is verified against the authored bytes instead.
    struct ReachSensorLearned
    {
        uintptr_t slot;
        bool have;
        float bx, by;
        uint32_t wx, wy;
    };
    constinit static ReachSensorLearned g_reachSensorLearned[16];
} // namespace
namespace reach_bridge
{
    bool ReachAuthoredSensorOrigin(uintptr_t slot, uint8_t out[8]) noexcept
    {
        uint32_t cx = 0, cy = 0;
        const volatile uint32_t* live = reinterpret_cast<const volatile uint32_t*>(slot - 52);
        cx = live[0];
        cy = live[1];
        for (const ReachSensorLearned& e : g_reachSensorLearned)
            if (e.have && e.slot == slot && e.wx == cx && e.wy == cy)
            {
                memcpy(out, &e.bx, 4);
                memcpy(out + 4, &e.by, 4);
                return true;
            }
        return false;
    }
} // namespace reach_bridge
namespace
{
    // HREK collision_test_vector (0x41B960), matched independently to retail
    // 0x12969C. The old endpoint resolver remains callable but is too rare to
    // schedule sustained contact; its logs contain zero-tick windows.
    __declspec(noinline) uint8_t __fastcall ReachCollisionVectorDetour(uint64_t flags, int32_t mode, const float* start,
        const float* vector, int32_t ignoredObjectA, int32_t ignoredObjectB, int32_t ignoredObjectC, void* result)
    {
        g_reachWorldCollision.callbacks.fetch_add(1, std::memory_order_acq_rel);
        uint8_t nativeResult = 0;
        const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
        __try
        {
            if (!ReachRedirectContactVector(
                    caller, flags, mode, ignoredObjectA, ignoredObjectB, ignoredObjectC, result, nativeResult))
                nativeResult = LegacyCollisionSchedulerDetourBody(g_reachWorldCollision, flags, mode, start, vector,
                    ignoredObjectA, ignoredObjectB, ignoredObjectC, result);
        }
        __finally
        {
            g_reachWorldCollision.callbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
        return nativeResult;
    }
} // namespace
namespace reach_bridge
{
    bool ReachBoneMatrixFinite(const BoneMatrix& matrix);
} // namespace reach_bridge
namespace
{
    // The weapon controller's game-space basis only. Unlike ControllerWorldPoseEx
    // it does not need the Halo 3/ODST camera globals (g_camValid/g_baseCamValid),
    // which Reach never sets: the Reach scope pass only uses the basis (roll).
    // No persistent-support-grip one-hand override here: ReachPrepareScope runs
    // before ReachBeginFpPairScope arms g_fpStereoSolveScope, so it is never armed.
    bool ControllerWorldBasis(float basis[9])
    {
        if (!g_vrAim.load() || !g_enabled.load())
            return false;
        float cq[4], cp[3];
        if (!VR_GetAimPose(cq, cp))
            return false;
        float hullYaw = 0.0f, hullPitch = 0.0f;
        const bool followValid = Halo3ReadRollStableFollow(hullYaw, hullPitch);
        BuildTrackedGameBasisFromFrame(cq, false, followValid, hullYaw, hullPitch, basis);
        for (int i = 0; i < 9; ++i)
            if (!std::isfinite(basis[i]))
                return false;
        return true;
    }
} // namespace
namespace reach_bridge
{
    bool ReachSeatUsesWheel();
    bool ReachSeatAuthorsSteeringNow();
} // namespace reach_bridge
namespace
{
    // ------------------------------------------------------------------
    // Halo: Reach per-eye stereo camera core.
    //
    // Reach is a permanent title. Its render path was proven statically and
    // corroborated by the read-only observer:
    // the exact inner renderer player_view_render (RVA 0x26C6DC), the outer
    // main_render_view (RVA 0x0C31F4) with its normal vs screenshot caller
    // returns, the 0x2B0 rasterizer-camera workspace whose primary compact
    // camera lives at +0x000 (Reach layout: position +0x00, forward +0x0C, up
    // +0x18, vertical FOV +0x28 -- exactly what ValidateReachCompactCamera
    // decodes), and the player-view rollback regions.
    //
    // The worker installs the five proven camera/FP hooks only after the
    // loaded-image preflight passes and the VR eye-capture resources are
    // published, then arms after a one-second fresh-camera safety interval,
    // exactly like the accepted ODST core. A failed owned eye is invalidated and
    // never published as Reach stereo. The one-original-render safety call stays
    // outside that completed pair and cannot become a partial VR mode.
    // Camera-injection correctness remains an exact-hash headset result.
    using ReachPlayerViewRenderFn = void(__fastcall*)(uintptr_t);
    using ReachMainRenderViewFn = uintptr_t(__fastcall*)(uintptr_t, uintptr_t, uint32_t);

    // The four stock camera-rebuild helpers, with the exact ABIs proven
    // statically. Running them per eye before the
    // inner render rebuilds player_view+0x490 from the VR camera, exactly as
    // Halo 3's RenderViewHook rebuilds view+0x98 with the engine's own helpers.
    using ReachFrustumHelperFn = bool(__fastcall*)(void*, float*);
    using ReachProjectionBuilderFn = void(__fastcall*)(void*, float*, void*, float);
    using ReachCameraStateUpdaterFn = void(__fastcall*)(void*, void*);
    using ReachMatrixBuilderFn = void(__fastcall*)(void*, void*, void*, void*, void*);
    using ReachCameraStackCallbackFn = void(__fastcall*)();
    using ReachFpCameraRebuildFn = void(__fastcall*)(void*, bool);
    using ReachFpCameraUploadFn = void(__fastcall*)(void*, void*);

    static_assert(sizeof(ReachNativeUnitCameraInfo) == 0x20);
    static_assert(offsetof(ReachNativeUnitCameraInfo, seatCamera) == 0x08);
    static_assert(offsetof(ReachNativeUnitCameraInfo, worldCameraPoint) == 0x10);

    struct ReachVehicleFrame
    {
        bool active = false;
        bool carrierBasisValid = false;
        bool entryForwardValid = false;
        bool unitAimValid = false;
        int32_t unitHandle = -1;
        int32_t directParent = -1;
        int32_t carrier = -1;
        int32_t seatIndex = -1;
        uint32_t definitionDatum = 0xFFFFFFFFu;
        ReachVehicleId identity = ReachVehicleId::Unknown;
        uint32_t seatFlags = 0;
        float cameraBase[3]{};
        float handsBase[3]{};
        float rawCarrierForward[3]{};
        float entryForward[3]{};
        float unitAimForward[3]{};
    };

    struct ReachRenderHelpers
    {
        ReachFrustumHelperFn frustum = nullptr;
        ReachProjectionBuilderFn projection = nullptr;
        ReachCameraStateUpdaterFn cameraState = nullptr;
        ReachMatrixBuilderFn matrix = nullptr;
        ReachCameraStackCallbackFn commitOuterCamera = nullptr;
        bool Ready() const { return frustum && projection && cameraState && matrix && commitOuterCamera; }
    };
    constinit ReachRenderHelpers g_reachHelpers;

    // A direct `call rel32` is `E8 <int32>`; its target is site + 5 + rel32.
    // Verifying the two proven setup call sites resolve to the documented helper
    // RVAs anchors those addresses to real, in-image call edges so a changed
    // module fails open instead of arming on a stale offset.
    bool ReachVerifyRel32Call(uintptr_t base, uintptr_t siteRva, uintptr_t expectedTargetRva)
    {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(base + siteRva);
        if (p[0] != 0xE8)
            return false;
        int32_t rel = 0;
        memcpy(&rel, p + 1, sizeof(rel));
        const uintptr_t target = base + siteRva + 5 + static_cast<uintptr_t>(static_cast<intptr_t>(rel));
        return target == base + expectedTargetRva;
    }

    bool ReachVerifyRipRelativeLea(uintptr_t base, uintptr_t siteRva, uint8_t opcode0, uint8_t opcode1, uint8_t opcode2,
        uintptr_t expectedTargetRva)
    {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(base + siteRva);
        if (p[0] != opcode0 || p[1] != opcode1 || p[2] != opcode2)
            return false;
        int32_t displacement = 0;
        memcpy(&displacement, p + 3, sizeof(displacement));
        const uintptr_t target = static_cast<uintptr_t>(static_cast<intptr_t>(base + siteRva + 7) + displacement);
        return target == base + expectedTargetRva;
    }

    // The complete main_render_view body is already hash-pinned by the cold
    // preflight. Recheck its live visibility edges and the callee's exact
    // secondary-camera load before authorizing pre-visibility mutation, so an
    // in-memory mismatch remains stock instead of silently culling from an
    // unknown camera object.
    bool ReachVerifyVisibilityConsumer(uintptr_t base, size_t size)
    {
        constexpr uintptr_t kWorkspaceLeaRva = 0x000C3319;
        constexpr uint8_t kSecondaryCompactLea[7]{0x48, 0x8D, 0x8A, 0x54, 0x01, 0x00, 0x00};
        const auto fits = [size](uintptr_t rva, size_t bytes) { return rva < size && bytes <= size - rva; };
        if (!fits(kWorkspaceLeaRva, 7) || !fits(kReachVisibilityClusterLookupCallRva, 5) ||
            !fits(kReachVisibilitySecondaryCompactLeaRva, sizeof(kSecondaryCompactLea)) ||
            !fits(kReachVisibilitySecondaryDerivedLeaRva, 7) || !fits(kReachVisibilityBuildCallRva, 5))
        {
            return false;
        }
        return ReachVerifyRipRelativeLea(base, kWorkspaceLeaRva, 0x48, 0x8D, 0x15, kReachDefaultWorkspaceRva) &&
               ReachVerifyRel32Call(
                   base, kReachVisibilityClusterLookupCallRva, kReachVisibilityClusterLookupTargetRva) &&
               memcmp(reinterpret_cast<const void*>(base + kReachVisibilitySecondaryCompactLeaRva),
                   kSecondaryCompactLea, sizeof(kSecondaryCompactLea)) == 0 &&
               ReachVerifyRipRelativeLea(base, kReachVisibilitySecondaryDerivedLeaRva, 0x4C, 0x8D, 0x05,
                   kReachVisibilitySecondaryDerivedAddressRva) &&
               ReachVerifyRel32Call(base, kReachVisibilityBuildCallRva, kReachVisibilityBuildTargetRva);
    }

    // Decode the exact internal patchy-fog skip gate from the already
    // hash-pinned player_view_render body. This cold install-time check binds
    // the writable byte to the retail RIP-relative test and the proven helper
    // call edge. A changed opcode, branch, target, or mapping leaves Reach
    // completely stock.
    bool ReachVerifyPatchyFogGate(uintptr_t base, size_t size, uint8_t*& flags)
    {
        flags = nullptr;
        constexpr size_t kGateBytes = 17;
        const auto fits = [size](uintptr_t rva, size_t bytes) { return rva < size && bytes <= size - rva; };
        if (!fits(kReachPatchyFogGateTestRva, kGateBytes) || !fits(kReachPatchyFogFlagsRva, sizeof(uint8_t)) ||
            !fits(kReachPatchyFogCallRva, 5))
        {
            return false;
        }

        const uint8_t* gate = reinterpret_cast<const uint8_t*>(base + kReachPatchyFogGateTestRva);
        constexpr uint8_t kTail[] = {kReachPatchyFogSkipMask, 0x75, 0x08, 0x48, 0x8B, 0xCE, 0xE8};
        if (gate[0] != 0xF6 || gate[1] != 0x05 || memcmp(gate + 6, kTail, sizeof(kTail)) != 0 ||
            kReachPatchyFogSkipJumpRva != kReachPatchyFogGateTestRva + 7 ||
            kReachPatchyFogCallRva != kReachPatchyFogGateTestRva + 12)
        {
            return false;
        }

        int32_t displacement = 0;
        memcpy(&displacement, gate + 2, sizeof(displacement));
        const uintptr_t next = base + kReachPatchyFogGateTestRva + 7;
        const uintptr_t target = static_cast<uintptr_t>(static_cast<intptr_t>(next) + displacement);
        if (target != base + kReachPatchyFogFlagsRva ||
            !ReachVerifyRel32Call(base, kReachPatchyFogCallRva, kReachPatchyFogTargetRva))
        {
            return false;
        }

        auto* resolved = reinterpret_cast<uint8_t*>(target);
        uint8_t current = 0;
        if (!SafeReadByte(resolved, &current))
            return false;
        flags = resolved;
        return true;
    }

    constexpr size_t kReachCompactCameraBytes = 0x90;

    struct ReachEyeRenderInput
    {
        float position[3]{};
        ReachEyeCullFrustum frustum{};
        ReachSymmetricFovCover rasterCover{};
    };

    struct ReachPendingSeatRecenter
    {
        bool valid = false;
        ReachSeatLeaseKey key{};
        float gameYaw = 0.0f;
        float headYaw = 0.0f;
        float headPosition[3]{};
    };

#pragma warning(push)
#pragma warning(disable : 4324) // alignas(16) camera buffers: the padding is intended
    struct ReachScopeFrame
    {
        bool ready = false;
        alignas(16) unsigned char compact[kReachCompactCameraBytes]{};
        alignas(16) unsigned char derived[kReachDerivedBlockSize]{};
    };
#pragma warning(pop)
    constinit thread_local bool g_reachScopeRendering = false;
} // namespace
namespace reach_bridge
{
    constinit std::atomic<uint32_t> g_reachScopeFaultGeneration{0};
} // namespace reach_bridge
namespace
{
    constinit std::atomic<uint32_t> g_reachScopeFaults{0};

    struct ReachOwnerScope
    {
        bool active = false;
        uintptr_t workspace = 0;
        uintptr_t playerView = 0;
        int32_t cameraStackDepthBefore = -1;
        uint64_t preparedSerial = 0;
        bool cutsceneTheater = false;
        float cutsceneTheaterAspect = 0.0f;
        ReachVrRenderAccess* renderAccess = nullptr;
        float gameplayBasePosition[3]{};
        float presentedCameraBasePosition[3]{};
        ReachVehicleFrame vehicle{};
        bool vehicleViewApplied = false;
        ReachPendingSeatRecenter pendingSeatRecenter{};
        FpExplicitPoseTargets fpTargets{};
        alignas(16) unsigned char headCenter[kReachCompactCameraBytes]{};
        ReachEyeRenderInput eyes[2]{};
        ReachScopeFrame scope{};
    };

    // Outer and inner hooks run on the same render thread in one synchronous
    // call stack, so a thread-local owner scope needs no cross-thread sync.
    constinit thread_local ReachOwnerScope g_reachOwnerScope;
    // A nested main_render_view must be entirely stock. Clearing the owner
    // scope alone is insufficient if that nested call recursively enters the
    // outer hook again, so carry an explicit suppression bit through the whole
    // nested stock call tree.
    constinit thread_local bool g_reachNestedOuterSuppressed = false;

#pragma warning(push)
#pragma warning(disable : 4324) // alignas(16) camera buffers: the padding is intended
    struct ReachFpCameraEyeScope
    {
        bool active = false;
        bool chudParityFailed = false;
        bool chudClass2Seen = false;
        bool authoredCrosshairCaptured = false;
        // Identity of the art captured this eye: which weapon definition,
        // which collection, which widgets. Static art produces the same key
        // every frame, which is what lets the per-frame upload be skipped.
        uint64_t captureKey = 0;
        // How many class-2 widget pieces actually reached the capture surface
        // this eye. Reach's crosshair does not stop cleanly - a preserved
        // log shows its class-2 draws THINNING (a two-second window
        // uploading 19 instead of 30) before reaching zero - so a frame can
        // legitimately capture only part of the reticle. Publishing that part
        // replaces good art with a fragment, which is what the player sees as
        // the crosshair disappearing.
        uint32_t captureDraws = 0;
        uint32_t generation = 0;
        uint32_t eye = 0;
        uint64_t preparedSerial = 0;
        uintptr_t workspace = 0;
        uintptr_t playerView = 0;
        alignas(16) unsigned char compact[kReachCompactCameraSize]{};
        alignas(16) unsigned char derived[kReachDerivedBlockSize]{};
    };
#pragma warning(pop)
    constinit thread_local ReachFpCameraEyeScope g_reachFpCameraEyeScope;
} // namespace
namespace reach_bridge
{
    constinit std::atomic<uint32_t> g_reachWindReplayFaults{0};
} // namespace reach_bridge
namespace
{
    constinit std::atomic<uint32_t> g_reachWindReplayPairs{0};
} // namespace
namespace reach_bridge
{
    constinit ReachCameraCore g_reachCamera;

    uint32_t ReachRetainedRuntimeGeneration()
    {
        return g_reachCamera.generation.load(std::memory_order_acquire);
    }
    // High 32 bits are the title-module generation; low 32 bits are the
    // ReachVehicleInputState value. One atomic publication prevents the input
    // thread from combining a new generation with an old vehicle result.
    constinit std::atomic<uint64_t> g_reachVehicleInputSnapshot{0};
} // namespace reach_bridge
namespace
{
    constinit std::atomic<uint32_t> g_reachVehicleInputFaults{0};
    constinit std::atomic<uint64_t> g_reachVehicleTrimSnapshot{0};
} // namespace
namespace reach_bridge
{
    constinit std::atomic<uint64_t> g_reachVehicleSampleMs{0};
} // namespace reach_bridge
namespace
{
    constinit std::atomic<uint32_t> g_reachVehicleFpStable{static_cast<uint32_t>(Halo3VehicleState::Unknown)};
    constinit std::atomic<uint32_t> g_reachVehicleCameraFaults{0};
    constinit std::atomic<uint32_t> g_reachVehicleIdentityMisses{0};
    constinit std::atomic<uint32_t> g_reachVehicleMarkerFailures{0};
    constinit std::atomic<uint32_t> g_reachVehicleBoundsFailures{0};
    constinit std::atomic<uint32_t> g_reachVehicleBounceFallbacks{0};
    constinit std::atomic<uint32_t> g_reachVehicleFollowFallbacks{0};
    constinit std::atomic<uint32_t> g_reachVehicleSeatPatchState{0};
    constinit std::atomic<uint32_t> g_reachVehicleSeatPatchSerial{0};
    constinit std::atomic<uint32_t> g_reachVehicleSeatPatchFailures{0};
    constinit std::atomic<uint32_t> g_reachUnitCameraHideSets{0};
    constinit std::atomic<uint32_t> g_reachUnitCameraHideAlreadySet{0};
    constinit std::atomic<uint32_t> g_reachUnitCameraHideRestores{0};
    constinit std::atomic<uint32_t> g_reachUnitCameraHideFailures{0};
    constinit std::atomic<uint32_t> g_reachVehicleSeatRecenters{0};
    constinit std::atomic<uint32_t> g_reachVehicleExitRecenters{0};
    constinit std::atomic<uint32_t> g_reachSeatAuthorsSteering{0};
    constinit std::atomic<uint64_t> g_reachYawReferencePair{kReachInvalidYawReferencePair};

    void ReachPublishYawReferencePair(float gameYaw, float headYaw)
    {
        if (!isfinite(gameYaw) || !isfinite(headYaw))
            return;
        g_reachYawReferencePair.store(ReachPackYawReferencePair(gameYaw, headYaw), std::memory_order_release);
    }
} // namespace
namespace reach_bridge
{
    bool ReachReadYawReferencePair(float& gameYaw, float& headYaw)
    {
        const ReachYawReferencePair pair =
            ReachUnpackYawReferencePair(g_reachYawReferencePair.load(std::memory_order_acquire));
        if (!ReachYawReferencePairValid(pair))
            return false;
        gameYaw = pair.gameYaw;
        headYaw = pair.headYaw;
        return true;
    }
} // namespace reach_bridge
namespace
{
    // Reach's camera thread publishes one coherent aim direction for the input
    // thread. Unlike the shared legacy three-float publication, this cannot
    // combine components from adjacent frames after the ready flag is already
    // true. The source identifies the deliberate per-frame fallback.
    struct ReachAimFeedbackPublication
    {
        std::atomic<uint32_t> sequence{0};
        std::atomic<uint32_t> generation{0};
        std::atomic<uint64_t> sampleMs{0};
        std::atomic<uint8_t> source{static_cast<uint8_t>(ReachAimFeedbackSource::OnFootCompact)};
        std::atomic<float> x{1.0f};
        std::atomic<float> y{0.0f};
        std::atomic<float> z{0.0f};
    };
    constinit ReachAimFeedbackPublication g_reachAimFeedback;
    constinit std::atomic<uint32_t> g_reachUnitAimSamples{0};
    constinit std::atomic<uint32_t> g_reachUnitAimFallbacks{0};
} // namespace
namespace reach_bridge
{
    constinit std::atomic<uint32_t> g_reachAimFeedbackReadFallbacks{0};
} // namespace reach_bridge
namespace
{
    constinit std::atomic<uint32_t> g_reachReticleAimFallbacks{0};

    // The compositor may consume only aim from the exact stereo pair it is
    // about to submit. This separate publication is written after both Reach
    // eyes render and copy successfully; the older world-space publication
    // above remains the independent input/steering feedback path.
    struct ReachCompletedReticleAimPublication
    {
        std::atomic<uint32_t> sequence{0};
        std::atomic<uint32_t> generation{0};
        std::atomic<uint64_t> preparedSerial{0};
        std::atomic<uint8_t> source{static_cast<uint8_t>(ReachAimFeedbackSource::OnFootCompact)};
        std::atomic<int32_t> unitHandle{-1};
        std::atomic<int32_t> directParent{-1};
        std::atomic<uint32_t> definitionDatum{0xFFFFFFFFu};
        std::atomic<int32_t> seatIndex{-1};
        std::atomic<uint8_t> identity{static_cast<uint8_t>(ReachVehicleId::Unknown)};
        std::atomic<float> x{0.0f};
        std::atomic<float> y{0.0f};
        std::atomic<float> z{-1.0f};
    };
    constinit ReachCompletedReticleAimPublication g_reachCompletedReticleAim;

    // Personal-weapon firing runs outside the render callback. Two coherent
    // records keep its origin fail-open: the current full-salt occupation and
    // the rendered center eye from a completed stereo pair. The firing hook
    // substitutes only when both fresh records carry the exact same key.
    struct ReachShotOccupationPublication
    {
        std::atomic<uint32_t> sequence{0};
        std::atomic<uint32_t> generation{0};
        std::atomic<uint64_t> sampleMs{0};
        std::atomic<uint8_t> active{0};
        std::atomic<uint8_t> allowsWeapons{0};
        std::atomic<int32_t> unitHandle{-1};
        std::atomic<int32_t> directParent{-1};
        std::atomic<uint32_t> definitionDatum{0xFFFFFFFFu};
        std::atomic<int32_t> seatIndex{-1};
    };
    constinit ReachShotOccupationPublication g_reachShotOccupation;

    struct ReachCompletedEyePublication
    {
        std::atomic<uint32_t> sequence{0};
        std::atomic<uint32_t> generation{0};
        std::atomic<uint64_t> preparedSerial{0};
        std::atomic<uint64_t> sampleMs{0};
        std::atomic<int32_t> unitHandle{-1};
        std::atomic<int32_t> directParent{-1};
        std::atomic<uint32_t> definitionDatum{0xFFFFFFFFu};
        std::atomic<int32_t> seatIndex{-1};
        std::atomic<float> x{0.0f};
        std::atomic<float> y{0.0f};
        std::atomic<float> z{0.0f};
    };
    constinit ReachCompletedEyePublication g_reachCompletedEye;

    // World-space point occupied by the floating controller reticle that was
    // actually presented after aim stabilization. Vehicle fire reads this
    // independently from the current occupation and accepts it only when the
    // full salted seat key and title generation still match.
    struct ReachCompletedReticleTargetPublication
    {
        std::atomic<uint32_t> sequence{0};
        std::atomic<uint32_t> generation{0};
        std::atomic<uint64_t> sampleMs{0};
        std::atomic<int32_t> unitHandle{-1};
        std::atomic<int32_t> directParent{-1};
        std::atomic<uint32_t> definitionDatum{0xFFFFFFFFu};
        std::atomic<int32_t> seatIndex{-1};
        std::atomic<float> x{0.0f};
        std::atomic<float> y{0.0f};
        std::atomic<float> z{0.0f};
        // The ORIGIN of the same presented sight ray, in the same game
        // space as the target above. A seated personal shot must leave this
        // point, not the rendered eye: the crosshair rides the controller ray,
        // so an eye-origin shot only crosses the sight at crosshair_distance_m
        // and misses it at every other range.
        std::atomic<float> originX{0.0f};
        std::atomic<float> originY{0.0f};
        std::atomic<float> originZ{0.0f};
        // Producer receipt of the ray that was actually published, so a
        // weapon-sensitive consumer that reads this point later re-qualifies
        // it against its own invocation instead of trusting an old point.
        std::atomic<uint64_t> supportEpoch{0};
        std::atomic<uint8_t> supportTrusted{0};
        std::atomic<uint64_t> raySerial{0};
    };
    constinit ReachCompletedReticleTargetPublication g_reachCompletedReticleTarget;
    constinit ReachCompletedReticleTargetPublication g_reachOnFootShotRay;
} // namespace
namespace reach_bridge
{
    constinit std::atomic<uint32_t> g_reachHandShotMoves{0};
    constinit std::atomic<uint32_t> g_reachHandShotMisses{0};
} // namespace reach_bridge
namespace
{
    void ReachClearAimFeedback()
    {
        auto& published = g_reachAimFeedback;
        published.sequence.fetch_add(1, std::memory_order_acq_rel);
        published.generation.store(0, std::memory_order_relaxed);
        published.sampleMs.store(0, std::memory_order_relaxed);
        published.source.store(static_cast<uint8_t>(ReachAimFeedbackSource::OnFootCompact), std::memory_order_relaxed);
        published.x.store(1.0f, std::memory_order_relaxed);
        published.y.store(0.0f, std::memory_order_relaxed);
        published.z.store(0.0f, std::memory_order_relaxed);
        published.sequence.fetch_add(1, std::memory_order_release);
    }

    void ReachClearCompletedFrameFeedback()
    {
        g_reachOnFootShotRay.generation.store(0, std::memory_order_release);
        g_reachOnFootShotRay.supportEpoch.store(0, std::memory_order_relaxed);
        g_reachOnFootShotRay.supportTrusted.store(0, std::memory_order_relaxed);
        g_reachOnFootShotRay.raySerial.store(0, std::memory_order_relaxed);
        {
            auto& published = g_reachCompletedReticleAim;
            published.sequence.fetch_add(1, std::memory_order_acq_rel);
            published.generation.store(0, std::memory_order_relaxed);
            published.preparedSerial.store(0, std::memory_order_relaxed);
            published.source.store(
                static_cast<uint8_t>(ReachAimFeedbackSource::OnFootCompact), std::memory_order_relaxed);
            published.unitHandle.store(-1, std::memory_order_relaxed);
            published.directParent.store(-1, std::memory_order_relaxed);
            published.definitionDatum.store(0xFFFFFFFFu, std::memory_order_relaxed);
            published.seatIndex.store(-1, std::memory_order_relaxed);
            published.identity.store(static_cast<uint8_t>(ReachVehicleId::Unknown), std::memory_order_relaxed);
            published.x.store(0.0f, std::memory_order_relaxed);
            published.y.store(0.0f, std::memory_order_relaxed);
            published.z.store(-1.0f, std::memory_order_relaxed);
            published.sequence.fetch_add(1, std::memory_order_release);
        }
        {
            auto& published = g_reachCompletedEye;
            published.sequence.fetch_add(1, std::memory_order_acq_rel);
            published.generation.store(0, std::memory_order_relaxed);
            published.preparedSerial.store(0, std::memory_order_relaxed);
            published.sampleMs.store(0, std::memory_order_relaxed);
            published.unitHandle.store(-1, std::memory_order_relaxed);
            published.directParent.store(-1, std::memory_order_relaxed);
            published.definitionDatum.store(0xFFFFFFFFu, std::memory_order_relaxed);
            published.seatIndex.store(-1, std::memory_order_relaxed);
            published.x.store(0.0f, std::memory_order_relaxed);
            published.y.store(0.0f, std::memory_order_relaxed);
            published.z.store(0.0f, std::memory_order_relaxed);
            published.sequence.fetch_add(1, std::memory_order_release);
        }
        {
            auto& published = g_reachCompletedReticleTarget;
            published.sequence.fetch_add(1, std::memory_order_acq_rel);
            published.generation.store(0, std::memory_order_relaxed);
            published.sampleMs.store(0, std::memory_order_relaxed);
            published.unitHandle.store(-1, std::memory_order_relaxed);
            published.directParent.store(-1, std::memory_order_relaxed);
            published.definitionDatum.store(0xFFFFFFFFu, std::memory_order_relaxed);
            published.seatIndex.store(-1, std::memory_order_relaxed);
            published.x.store(0.0f, std::memory_order_relaxed);
            published.y.store(0.0f, std::memory_order_relaxed);
            published.z.store(0.0f, std::memory_order_relaxed);
            published.supportEpoch.store(0, std::memory_order_relaxed);
            published.supportTrusted.store(0, std::memory_order_relaxed);
            published.raySerial.store(0, std::memory_order_relaxed);
            published.sequence.fetch_add(1, std::memory_order_release);
        }
    }

    void ReachPublishShotOccupation(uint32_t generation, const ReachVehicleFrame* frame)
    {
        const bool active = generation && frame && frame->active;
        auto& published = g_reachShotOccupation;
        published.sequence.fetch_add(1, std::memory_order_acq_rel);
        published.generation.store(active ? generation : 0, std::memory_order_relaxed);
        published.sampleMs.store(active ? GetTickCount64() : 0, std::memory_order_relaxed);
        published.active.store(active ? 1u : 0u, std::memory_order_relaxed);
        published.allowsWeapons.store(
            active && (frame->seatFlags & kReachSeatAllowsWeaponsBit) != 0 ? 1u : 0u, std::memory_order_relaxed);
        published.unitHandle.store(active ? frame->unitHandle : -1, std::memory_order_relaxed);
        published.directParent.store(active ? frame->directParent : -1, std::memory_order_relaxed);
        published.definitionDatum.store(active ? frame->definitionDatum : 0xFFFFFFFFu, std::memory_order_relaxed);
        published.seatIndex.store(active ? frame->seatIndex : -1, std::memory_order_relaxed);
        published.sequence.fetch_add(1, std::memory_order_release);
    }

    void ReachPublishAimFeedback(uint32_t generation, ReachAimFeedbackSource source, const float forward[3])
    {
        if (!generation || !forward || !std::isfinite(forward[0]) || !std::isfinite(forward[1]) ||
            !std::isfinite(forward[2]))
        {
            return;
        }
        auto& published = g_reachAimFeedback;
        published.sequence.fetch_add(1, std::memory_order_acq_rel);
        published.generation.store(generation, std::memory_order_relaxed);
        published.sampleMs.store(GetTickCount64(), std::memory_order_relaxed);
        published.source.store(static_cast<uint8_t>(source), std::memory_order_relaxed);
        published.x.store(forward[0], std::memory_order_relaxed);
        published.y.store(forward[1], std::memory_order_relaxed);
        published.z.store(forward[2], std::memory_order_relaxed);
        published.sequence.fetch_add(1, std::memory_order_release);
        if (source == ReachAimFeedbackSource::SeatedUnitAim)
            g_reachUnitAimSamples.fetch_add(1, std::memory_order_relaxed);
        else if (source == ReachAimFeedbackSource::SeatedCompactFallback)
            g_reachUnitAimFallbacks.fetch_add(1, std::memory_order_relaxed);
    }
} // namespace
namespace reach_bridge
{
    bool ReachReadAimFeedback(float out[3])
    {
        if (!out)
            return false;
        const uint32_t generation = g_reachCamera.generation.load(std::memory_order_acquire);
        const uint64_t nowMs = GetTickCount64();
        auto& published = g_reachAimFeedback;
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const uint32_t before = published.sequence.load(std::memory_order_acquire);
            if (!before || (before & 1u))
                continue;
            const uint32_t sampleGeneration = published.generation.load(std::memory_order_relaxed);
            const uint64_t sampleMs = published.sampleMs.load(std::memory_order_relaxed);
            const auto source = static_cast<ReachAimFeedbackSource>(published.source.load(std::memory_order_relaxed));
            const float sample[3] = {published.x.load(std::memory_order_relaxed),
                published.y.load(std::memory_order_relaxed), published.z.load(std::memory_order_relaxed)};
            if (published.sequence.load(std::memory_order_acquire) != before)
                continue;
            if (sampleGeneration != generation || !sampleMs || nowMs < sampleMs || nowMs - sampleMs > 500 ||
                source == ReachAimFeedbackSource::OnFootCompact || !std::isfinite(sample[0]) ||
                !std::isfinite(sample[1]) || !std::isfinite(sample[2]))
            {
                return false;
            }
            memcpy(out, sample, sizeof(sample));
            return true;
        }
        return false;
    }
} // namespace reach_bridge
namespace
{
    // The 2026-08-04 session produced 21 seated periods that stayed stock with
    // no logged reason: the identity-miss line dedups once per generation and
    // names nothing. These fixed slots let the hot frame builder publish the
    // exact tuple/type/seat the engine holds for each distinct unmatched or
    // out-of-census seat, and the 50 ms worker logs each one exactly once.
    // Allocation-free and lock-free by construction; slots never recycle
    // within a generation (8 distinct offenders per session is generous).
    struct ReachVehicleCensusMissSample
    {
        // 0 empty, 1 claimed (being filled), 2 ready to log, 3 logged.
        std::atomic<uint32_t> state{0};
        ReachVehicleFingerprint fingerprint{};
        int32_t observedType = 0;
        int32_t seatIndex = -1;
        int32_t seatCount = -1;
        uint8_t knownIdentity = 0;
    };
    constinit std::array<ReachVehicleCensusMissSample, 8> g_reachVehicleCensusMisses;

    void ReachPublishVehicleCensusMiss(const ReachVehicleFingerprint& fingerprint, int32_t observedType,
        int32_t seatIndex, int32_t seatCount, uint8_t knownIdentity)
    {
        for (auto& slot : g_reachVehicleCensusMisses)
        {
            if (slot.state.load(std::memory_order_acquire) < 2)
                continue;
            if (ReachVehicleFingerprintEqual(slot.fingerprint, fingerprint) && slot.seatIndex == seatIndex &&
                slot.knownIdentity == knownIdentity)
            {
                return;
            }
        }
        for (auto& slot : g_reachVehicleCensusMisses)
        {
            uint32_t expected = 0;
            if (!slot.state.compare_exchange_strong(expected, 1, std::memory_order_acq_rel))
            {
                continue;
            }
            slot.fingerprint = fingerprint;
            slot.observedType = observedType;
            slot.seatIndex = seatIndex;
            slot.seatCount = seatCount;
            slot.knownIdentity = knownIdentity;
            slot.state.store(2, std::memory_order_release);
            return;
        }
    }

    // The Reach shot line, the exact ODST shot-line design with Reach-native
    // evidence. HREK proves a seated occupant's
    // shots leave (unit camera-position evaluator result, unit aiming vector):
    // the firing path copies the evaluator's answer into a PRIVATE stack
    // buffer and projects the muzzle onto that ray, so the shot origin can
    // move without feeding back into the engine camera. The evaluator has 34
    // retail call sites and only TWO are firing; substitution happens only
    // when the return address matches one of those two, the unit is the
    // seated local player's own, the seat's tag allows a personal weapon
    // (drivers and mounted gunners fire from a vehicle barrel), no authored
    // cinematic is running, and a fresh finite rendered eye exists.
    using ReachUnitCameraPositionFn = uint64_t(__fastcall*)(int32_t, float*);
    constinit ReachUnitCameraPositionFn g_origReachUnitCameraPosition = nullptr;
    constinit void* g_reachUnitCameraPositionTarget = nullptr;
    // Atomics let teardown revoke the optional substitution before disabling
    // its hook without racing a firing thread already inside the detour.
    constinit std::atomic<uintptr_t> g_reachFiringOriginReturnA{0};
    constinit std::atomic<uintptr_t> g_reachFiringOriginReturnB{0};
    constinit std::atomic<uint32_t> g_reachCinematicLocked{0};
    constinit std::atomic<uint32_t> g_reachFiringOriginMoves{0};
    constinit std::atomic<uint32_t> g_reachFiringOriginSkips{0};
    // Why a personal seated shot kept its stock origin. Exactly one of these
    // advances per skipped firing call; the worker prints them together.
    constinit std::atomic<uint32_t> g_reachFiringOriginSkipTorn{0};
    constinit std::atomic<uint32_t> g_reachFiringOriginSkipNotSeated{0};
    constinit std::atomic<uint32_t> g_reachFiringOriginSkipNoWeaponSeat{0};
    constinit std::atomic<uint32_t> g_reachFiringOriginSkipKeyMismatch{0};
    constinit std::atomic<uint32_t> g_reachFiringOriginSkipStaleEye{0};
    constinit std::atomic<uint32_t> g_reachFiringOriginSkipNoSightRay{0};
    // The occupied seat's own flags word, published by the engine
    // thread that read it so the worker can report it honestly.
    constinit std::atomic<uint32_t> g_reachVehicleSeatFlagsPublished{0};
    // How many times Reach's own first-person interpolation/palette
    // transaction ran while a vehicle seat was occupied. This is the
    // one number that separates "the engine never built the occupant's
    // arms and weapon" from "it built them and we placed or collapsed
    // them wrong", and no previous report could tell those apart.
    constinit std::atomic<uint32_t> g_reachSeatedFpPaletteCalls{0};

    struct ReachCompletedEyeSnapshot
    {
        uint32_t generation = 0;
        uint64_t preparedSerial = 0;
        uint64_t sampleMs = 0;
        ReachSeatLeaseKey key{};
        float eye[3]{};
    };

    struct ReachCompletedReticleTargetSnapshot
    {
        uint32_t sequence = 0;
        uint32_t generation = 0;
        uint64_t sampleMs = 0;
        ReachSeatLeaseKey key{};
        float target[3]{};
        float origin[3]{};
        // Producer receipt frozen with this point.
        uint64_t supportEpoch = 0;
        bool supportTrusted = false;
        uint64_t raySerial = 0;
    };
} // namespace
namespace reach_bridge
{
    bool ReachReadShotOccupation(ReachShotOccupationSnapshot& out)
    {
        auto& published = g_reachShotOccupation;
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const uint32_t before = published.sequence.load(std::memory_order_acquire);
            if (!before || (before & 1u))
                continue;
            ReachShotOccupationSnapshot sample{};
            sample.sequence = before;
            sample.generation = published.generation.load(std::memory_order_relaxed);
            sample.sampleMs = published.sampleMs.load(std::memory_order_relaxed);
            sample.active = published.active.load(std::memory_order_relaxed) != 0;
            sample.allowsWeapons = published.allowsWeapons.load(std::memory_order_relaxed) != 0;
            sample.key = {sample.generation, published.unitHandle.load(std::memory_order_relaxed),
                published.directParent.load(std::memory_order_relaxed),
                published.definitionDatum.load(std::memory_order_relaxed),
                published.seatIndex.load(std::memory_order_relaxed)};
            if (published.sequence.load(std::memory_order_acquire) != before)
                continue;
            out = sample;
            return true;
        }
        return false;
    }
} // namespace reach_bridge
namespace
{
    bool ReachReadCompletedEye(ReachCompletedEyeSnapshot& out)
    {
        auto& published = g_reachCompletedEye;
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const uint32_t before = published.sequence.load(std::memory_order_acquire);
            if (!before || (before & 1u))
                continue;
            ReachCompletedEyeSnapshot sample{};
            sample.generation = published.generation.load(std::memory_order_relaxed);
            sample.preparedSerial = published.preparedSerial.load(std::memory_order_relaxed);
            sample.sampleMs = published.sampleMs.load(std::memory_order_relaxed);
            sample.key = {sample.generation, published.unitHandle.load(std::memory_order_relaxed),
                published.directParent.load(std::memory_order_relaxed),
                published.definitionDatum.load(std::memory_order_relaxed),
                published.seatIndex.load(std::memory_order_relaxed)};
            sample.eye[0] = published.x.load(std::memory_order_relaxed);
            sample.eye[1] = published.y.load(std::memory_order_relaxed);
            sample.eye[2] = published.z.load(std::memory_order_relaxed);
            if (published.sequence.load(std::memory_order_acquire) != before)
                continue;
            out = sample;
            return true;
        }
        return false;
    }

    // Consumer qualification, defined with the presented-ray producer below.
    // Declared here because the weapon-sensitive consumers that read a
    // previously published presented point run earlier in the file.
    bool ReachPresentedRayConsumable(GameTitle title, uint32_t generation, uint64_t consumerSerial,
        uint64_t raySupportEpoch, bool raySupportTrusted, uint64_t raySerial);

    bool ReachReadCompletedReticleTarget(ReachCompletedReticleTargetSnapshot& out,
        const ReachCompletedReticleTargetPublication& published = g_reachCompletedReticleTarget)
    {
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const uint32_t before = published.sequence.load(std::memory_order_acquire);
            if (!before || (before & 1u))
                continue;
            ReachCompletedReticleTargetSnapshot sample{};
            sample.sequence = before;
            sample.generation = published.generation.load(std::memory_order_relaxed);
            sample.sampleMs = published.sampleMs.load(std::memory_order_relaxed);
            sample.key = {sample.generation, published.unitHandle.load(std::memory_order_relaxed),
                published.directParent.load(std::memory_order_relaxed),
                published.definitionDatum.load(std::memory_order_relaxed),
                published.seatIndex.load(std::memory_order_relaxed)};
            sample.target[0] = published.x.load(std::memory_order_relaxed);
            sample.target[1] = published.y.load(std::memory_order_relaxed);
            sample.target[2] = published.z.load(std::memory_order_relaxed);
            sample.origin[0] = published.originX.load(std::memory_order_relaxed);
            sample.origin[1] = published.originY.load(std::memory_order_relaxed);
            sample.origin[2] = published.originZ.load(std::memory_order_relaxed);
            sample.supportEpoch = published.supportEpoch.load(std::memory_order_relaxed);
            sample.supportTrusted = published.supportTrusted.load(std::memory_order_relaxed) != 0;
            sample.raySerial = published.raySerial.load(std::memory_order_relaxed);
            if (published.sequence.load(std::memory_order_acquire) != before)
                continue;
            out = sample;
            return true;
        }
        return false;
    }

    // Runs on the engine's own weapon path. Seqlock reads plus three float
    // stores: no locks, allocation or logging. The original always runs first
    // and its register result is preserved. The full-key/current-generation
    // match prevents a prior seat or partial render from donating its eye.
    uint64_t ReachUnitCameraPositionBody(int32_t unitIndex, float* out, uintptr_t caller)
    {
        ReachUnitCameraPositionFn original = g_origReachUnitCameraPosition;
        const uint64_t stockResult = original ? original(unitIndex, out) : 0;
        if (!out)
            return stockResult;
        const uintptr_t returnA = g_reachFiringOriginReturnA.load(std::memory_order_relaxed);
        const uintptr_t returnB = g_reachFiringOriginReturnB.load(std::memory_order_relaxed);
        if (caller != returnA && caller != returnB)
            return stockResult; // not the shot line; leave it alone
        if (g_reachCinematicLocked.load(std::memory_order_relaxed))
            return stockResult; // authored camera owns the view

        ReachShotOccupationSnapshot occupation{};
        ReachCompletedEyeSnapshot renderedEye{};
        const uint64_t nowMs = GetTickCount64();
        const uint32_t generation = g_reachCamera.generation.load(std::memory_order_acquire);
        const bool coherent = ReachReadShotOccupation(occupation) && ReachReadCompletedEye(renderedEye) &&
                              g_reachShotOccupation.sequence.load(std::memory_order_acquire) == occupation.sequence;
        ReachShotOriginSample sample{};
        sample.currentGeneration = generation;
        sample.nowMs = nowMs;
        sample.occupationSampleMs = occupation.sampleMs;
        sample.renderedEyeSampleMs = renderedEye.sampleMs;
        sample.renderedEyePreparedSerial = renderedEye.preparedSerial;
        sample.active = occupation.active;
        sample.allowsWeapons = occupation.allowsWeapons;
        sample.firingUnitIndex = unitIndex;
        sample.occupation = occupation.key;
        sample.renderedEye = renderedEye.key;
        memcpy(sample.renderedEyePosition, renderedEye.eye, sizeof(sample.renderedEyePosition));
        // The evaluator receives a full unit datum. Its firing argument is
        // compared by object index, while both publications still carry and
        // compare the complete salted unit, parent, definition and seat key.
        if (!coherent || !ReachShotOriginSampleAdmitted(sample))
        {
            g_reachFiringOriginSkips.fetch_add(1, std::memory_order_relaxed);
            // One headset session skipped every personal shot and moved
            // none, and the single "SKIPPED" line could not say whether the
            // seat refused weapons, the keys disagreed or the eye was stale.
            // One lock-free increment on an already-rare firing call names it.
            std::atomic<uint32_t>* reason = &g_reachFiringOriginSkipTorn;
            if (!coherent)
                reason = &g_reachFiringOriginSkipTorn;
            else if (!sample.active)
                reason = &g_reachFiringOriginSkipNotSeated;
            else if (!sample.allowsWeapons)
                reason = &g_reachFiringOriginSkipNoWeaponSeat;
            else if (!ReachSeatLeaseKeyEqual(sample.occupation, sample.renderedEye))
                reason = &g_reachFiringOriginSkipKeyMismatch;
            else
                reason = &g_reachFiringOriginSkipStaleEye;
            reason->fetch_add(1, std::memory_order_relaxed);
            return stockResult;
        }
        // Leave from where the SIGHT starts, not from the eye.
        //
        // A headset run proved this substitution fires for a
        // passenger ("seated personal-weapon shots are leaving the rendered
        // eye", 01:52:57) and shots still did not follow
        // the crosshair. The rendered eye was the wrong point. In a Reach
        // vehicle the sight we present is the floating CONTROLLER reticle, and
        // ReachBuildPresentedReticleWorldTarget places it at
        // crosshair_distance_m along the controller ray. A shot leaving the eye
        // toward a point on the controller ray is a different line that merely
        // CROSSES the sight at that one distance; at every other range it is
        // off by the whole eye-to-hand offset - tens of centimetres, several
        // degrees at close range. Taking the ray's own origin makes the shot
        // line and the sight line the same line at every range, which is the
        // property the ODST work recorded as the whole point of moving an
        // origin at all.
        //
        // No silent fallback: without a fresh exact-key sight ray this shot
        // stays completely stock rather than leaving from a mismatched point.
        ReachCompletedReticleTargetSnapshot sight{};
        const bool sightRead = ReachReadCompletedReticleTarget(sight) &&
                               g_reachCompletedReticleTarget.sequence.load(std::memory_order_acquire) == sight.sequence;
        const bool sightUsable =
            sightRead && sight.sampleMs != 0 && sight.generation == generation && ReachSeatLeaseKeyValid(sight.key) &&
            ReachSeatLeaseKeyEqual(sight.key, occupation.key) && nowMs >= sight.sampleMs &&
            nowMs - sight.sampleMs <= kReachShotOriginFreshMs && std::isfinite(sight.origin[0]) &&
            std::isfinite(sight.origin[1]) && std::isfinite(sight.origin[2]) &&
            // The point is consumed later than it was produced; re-prove
            // the ray's producer receipt against this consuming invocation
            // (same owner, same epoch, serial staleness bound). A refused ray
            // leaves this shot completely stock - never a stale-owner origin.
            //
            // F3: the consuming frame's serial is the completed rendered eye's
            // prepared serial - the same frame whose occupation/freshness proof
            // (ReachShotOriginSampleAdmitted, above) admits this shot. The shot
            // origin itself is the presented sight ray, not that eye
            // position. Passing the ray's own serial here would make the
            // staleness bound tautological. The other two re-qualification
            // sites (the on-foot hand shot and the vehicle redirect) have no
            // consuming prepared serial in scope inside the weapon transaction,
            // so they deliberately keep the ray serial and document the
            // conservative latest-only-receipt behaviour.
            ReachPresentedRayConsumable(GameTitle::HaloReach, generation, sample.renderedEyePreparedSerial,
                sight.supportEpoch, sight.supportTrusted, sight.raySerial);
        if (!sightUsable)
        {
            g_reachFiringOriginSkips.fetch_add(1, std::memory_order_relaxed);
            g_reachFiringOriginSkipNoSightRay.fetch_add(1, std::memory_order_relaxed);
            return stockResult;
        }
        out[0] = sight.origin[0];
        out[1] = sight.origin[1];
        out[2] = sight.origin[2];
        g_reachFiringOriginMoves.fetch_add(1, std::memory_order_relaxed);
        return stockResult;
    }

    // The counter spans the complete original call as well as our feature
    // body. Worker-side frozen RIP scanning closes only the tiny relay/prologue
    // window before this increment; no trampoline or title-module pointer is
    // cleared until this scope has left.
    __declspec(noinline) uint64_t __fastcall ReachUnitCameraPositionHook(int32_t unitIndex, float* out)
    {
        const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
        uint64_t result = 0;
        g_reachCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        // Runtime ownership must follow a REAL Reach camera callback,
        // not the outer Present loop.  Save & Quit stops this path before the
        // pinned title DLL can unload, so this timestamp is also the clean
        // signal that the installed core has become stale.
        g_reachLastCamCopyMs.store(GetTickCount64(), std::memory_order_release);
        __try
        {
            result = ReachUnitCameraPositionBody(unitIndex, out, caller);
        }
        __finally
        {
            g_reachCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
        return result;
    }

    // Feature-local revocation is immediate and lock-free. The hook itself is
    // disabled, quiesced, and removed with every other Reach detour below: its
    // trampoline points into haloreach.dll and must never outlive the module.
    void RevokeReachFiringOriginFeature()
    {
        g_reachFiringOriginReturnA.store(0, std::memory_order_release);
        g_reachFiringOriginReturnB.store(0, std::memory_order_release);
    }

    // HREK weapons.cpp has exactly one call into this unit-adjust transaction
    // from the projectile builder. By the time it runs Reach has selected the
    // firing weapon, barrel, marker/origin and deepest controlling unit. The
    // original remains responsible for all of that. For the exact local seated
    // unit only, replace its central direction with the line from that native
    // origin through the floating reticle point; every downstream ballistic,
    // spread and tracking stage remains stock.
    using ReachUnitAdjustFn = void(__fastcall*)(int32_t unitIndex, float* origin, float* direction,
        const float* basisForward, const float* barrelOffset, const float* optionalCameraPoint, uint8_t firesFromCamera,
        uint8_t useUnitAim, uint8_t collision, uint32_t simulation);
    constinit ReachUnitAdjustFn g_origReachUnitAdjust = nullptr;
    constinit void* g_reachUnitAdjustTarget = nullptr;

    // Reach game brightness. haloreach.dll+0x252E28 is Reach's screen
    // colour/gamma publisher and the exact homologue of the Halo 3/ODST
    // function the headset already proved: same (a0, a1, a2) shuffle, same
    // imported `powf` against a rodata base, same (a0, powf) / (a1, a0*a1)
    // pairs. It keeps its own trampoline for the same cross-title
    // stale-pointer reason as ODST's.
    constinit HudXformFn g_origReachHudXform = nullptr;
    constinit void* g_reachHudXformTarget = nullptr;

    constinit std::atomic<bool> g_reachVehicleShotRedirectEnabled{false};
    constinit std::atomic<uint32_t> g_reachVehicleShotRedirects{0};
    constinit std::atomic<uint32_t> g_reachVehicleShotRedirectMisses{0};

    // HREK render_first_person_view.cpp has one output-user predicate before
    // either first-person render pass.  Reach's vehicle passenger camera leaves
    // that predicate non-NONE even when the seat independently allows personal
    // weapons, so the already-built weapon state never reaches interpolation or
    // the visible palette.  Override only that exact render caller while the
    // admitted outer owner is an allows-weapons passenger.  Simulation, camera,
    // steering and firing consumers of the same accessor remain stock.
    using ReachFirstPersonRenderGateFn = int32_t(__fastcall*)(int32_t);
    constinit ReachFirstPersonRenderGateFn g_origReachFirstPersonRenderGate = nullptr;
    constinit void* g_reachFirstPersonRenderGateTarget = nullptr;
} // namespace
namespace reach_bridge
{
    constinit std::atomic<uintptr_t> g_reachFirstPersonRenderGateReturn{0};
} // namespace reach_bridge
namespace
{
    constinit std::atomic<uint32_t> g_reachPassengerFirstPersonAdmissions{0};

    int32_t ReachFirstPersonRenderGateBody(int32_t outputUserIndex, uintptr_t caller)
    {
        ReachFirstPersonRenderGateFn original = g_origReachFirstPersonRenderGate;
        if (!original)
            return -1;
        const int32_t stock = original(outputUserIndex);
        if (g_reachScopeRendering && g_reachOwnerScope.active &&
            caller == g_reachFirstPersonRenderGateReturn.load(std::memory_order_acquire))
            return 0; // Exact native render predicate: non-NONE skips FP pixels.
        return stock;
    }

    __declspec(noinline) int32_t __fastcall ReachFirstPersonRenderGateHook(int32_t outputUserIndex)
    {
        const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
        int32_t result = -1;
        g_reachCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            result = ReachFirstPersonRenderGateBody(outputUserIndex, caller);
        }
        __finally
        {
            g_reachCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
        return result;
    }

    void ReachUnitAdjustBody(int32_t unitIndex, float* origin, float* direction, const float* basisForward,
        const float* barrelOffset, const float* optionalCameraPoint, uint8_t firesFromCamera, uint8_t useUnitAim,
        uint8_t collision, uint32_t simulation, bool& substituted)
    {
        // T-2 evidence only: whether one of this body's mod substitutions
        // produced the final ray. Never read by gameplay; the body's
        // substitution, collision-clip, receipt/admission and restore paths
        // are unchanged.
        substituted = false;
        ReachUnitAdjustFn original = g_origReachUnitAdjust;
        if (!original)
            return;
        original(unitIndex, origin, direction, basisForward, barrelOffset, optionalCameraPoint, firesFromCamera,
            useUnitAim, collision, simulation);
        // The same HREK-proven weapon transaction handles on-foot shots. Own
        // only the exact current local unit and a completed, fresh on-foot ray.
        // Reuse native world clipping from the already-safe stock origin to
        // the hand so extending through a wall cannot move a bullet behind it.
        if (origin && direction && g_enabled.load(std::memory_order_acquire) &&
            g_vrAim.load(std::memory_order_acquire) && VR_IsStereoEnabled() &&
            g_reachCamera.armed.load(std::memory_order_acquire) &&
            !g_reachCinematicLocked.load(std::memory_order_relaxed))
        {
            ReachCompletedReticleTargetSnapshot handRay{};
            ReachShotOccupationSnapshot occupationNow{};
            const bool onFootNow = ReachReadShotOccupation(occupationNow) && !occupationNow.active;
            const int32_t currentUnit = LegacyCollisionIgnoredObject(GameTitle::HaloReach);
            const uint32_t generation = g_reachCamera.generation.load(std::memory_order_acquire);
            bool nativeOnFoot = false;
            if (currentUnit != -1 && unitIndex == currentUnit && g_reachCamera.unitInVehicle)
            {
                __try
                {
                    nativeOnFoot = !g_reachCamera.unitInVehicle(currentUnit);
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    GUARD_COUNT("checking whether the Reach unit is on foot faulted");
                    nativeOnFoot = false;
                }
            }
            float handDirection[3]{};
            const bool owned = onFootNow && nativeOnFoot &&
                               ReachReadCompletedReticleTarget(handRay, g_reachOnFootShotRay) &&
                               // The published on-foot hand ray is consumed later than it
                               // was produced; re-prove its receipt against this consuming
                               // invocation, or leave the shot stock. No consuming
                               // prepared serial exists inside this weapon transaction, so the
                               // ray serial is passed deliberately; the receipt slot is
                               // latest-only, which is what refuses both an old ray and an old
                               // receipt (see the port document).
                               ReachPresentedRayConsumable(GameTitle::HaloReach, generation, handRay.raySerial,
                                   handRay.supportEpoch, handRay.supportTrusted, handRay.raySerial) &&
                               ReachBuildHandShotDirection(generation, handRay.generation, unitIndex, currentUnit,
                                   handRay.key.unitHandle, GetTickCount64(), handRay.sampleMs, handRay.origin,
                                   handRay.target, handDirection);
            if (onFootNow && unitIndex == currentUnit && currentUnit != -1 && !owned)
                g_reachHandShotMisses.fetch_add(1, std::memory_order_relaxed);
            if (owned)
            {
                auto& feature = g_reachWorldCollision;
                const float distance = Halo4WorldCollisionDistanceSquared(origin, handRay.origin);
                const float maximum = 1.5f * kReachWorldUnitsPerMeter;
                bool clipped = false;
                float accepted[3]{};
                if (feature.installed.load(std::memory_order_acquire) && feature.generation == generation &&
                    feature.resolver && distance >= 0 && distance <= maximum * maximum)
                {
                    memcpy(accepted, handRay.origin, sizeof(accepted));
                    const bool previousOwned = g_legacyCollisionOwnedQuery;
                    g_legacyCollisionOwnedQuery = true;
                    __try
                    {
                        reinterpret_cast<ReachCollisionResolveFn>(feature.resolver)(
                            origin, handRay.origin, accepted, unitIndex);
                        const float acceptedDistance = Halo4WorldCollisionDistanceSquared(origin, accepted);
                        clipped = acceptedDistance >= 0 && acceptedDistance <= maximum * maximum;
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER)
                    {
                        GUARD_COUNT("Reach's collision resolver faulted for the hand ray");
                        clipped = false;
                    }
                    g_legacyCollisionOwnedQuery = previousOwned;
                }
                if (clipped)
                {
                    memcpy(origin, accepted, sizeof(accepted));
                    memcpy(direction, handDirection, sizeof(handDirection));
                    g_reachHandShotMoves.fetch_add(1, std::memory_order_relaxed);
                    substituted = true; // T-2 evidence: the hand ray is final
                    return;
                }
                g_reachHandShotMisses.fetch_add(1, std::memory_order_relaxed);
            }
        }
        if (!origin || !direction || !g_reachVehicleShotRedirectEnabled.load(std::memory_order_acquire) ||
            g_reachCinematicLocked.load(std::memory_order_relaxed))
        {
            return;
        }

        ReachShotOccupationSnapshot occupation{};
        ReachCompletedReticleTargetSnapshot target{};
        const bool occupationRead = ReachReadShotOccupation(occupation);
        const bool targetRead = ReachReadCompletedReticleTarget(target);
        const bool occupationCoherent =
            occupationRead && g_reachShotOccupation.sequence.load(std::memory_order_acquire) == occupation.sequence;
        const bool targetCoherent =
            targetRead && g_reachCompletedReticleTarget.sequence.load(std::memory_order_acquire) == target.sequence;
        const bool coherent =
            occupationCoherent && targetCoherent &&
            // The presented point is consumed later than it was produced;
            // the receipt must still prove this consuming invocation's owner
            // against the live relationship, or the shot stays stock. As at
            // the on-foot hand-shot site, no consuming prepared serial exists
            // in this weapon transaction, so the ray serial is passed
            // deliberately (latest-only receipt => an old ray and an old
            // receipt are both refused).
            ReachPresentedRayConsumable(GameTitle::HaloReach, g_reachCamera.generation.load(std::memory_order_acquire),
                target.raySerial, target.supportEpoch, target.supportTrusted, target.raySerial);
        const uint32_t generation = g_reachCamera.generation.load(std::memory_order_acquire);
        const bool localCandidate = occupationCoherent && occupation.active && occupation.generation == generation &&
                                    occupation.key.unitHandle == unitIndex;

        ReachShotDirectionSample sample{};
        sample.currentGeneration = generation;
        sample.nowMs = GetTickCount64();
        sample.occupationSampleMs = occupation.sampleMs;
        sample.targetSampleMs = target.sampleMs;
        sample.active = occupation.active;
        sample.firingUnitIndex = unitIndex;
        sample.occupation = occupation.key;
        sample.targetKey = target.key;
        memcpy(sample.origin, origin, sizeof(sample.origin));
        memcpy(sample.target, target.target, sizeof(sample.target));
        if (!coherent || !ReachShotDirectionSampleAdmitted(sample))
        {
            if (localCandidate)
            {
                g_reachVehicleShotRedirectMisses.fetch_add(1, std::memory_order_relaxed);
            }
            return;
        }

        const float towardReticle[3] = {
            target.target[0] - origin[0], target.target[1] - origin[1], target.target[2] - origin[2]};
        const float lengthSquared = towardReticle[0] * towardReticle[0] + towardReticle[1] * towardReticle[1] +
                                    towardReticle[2] * towardReticle[2];
        if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-8f)
        {
            g_reachVehicleShotRedirectMisses.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        const float inverseLength = 1.0f / sqrtf(lengthSquared);
        const float normalized[3] = {
            towardReticle[0] * inverseLength, towardReticle[1] * inverseLength, towardReticle[2] * inverseLength};
        memcpy(direction, normalized, sizeof(normalized));
        g_reachVehicleShotRedirects.fetch_add(1, std::memory_order_relaxed);
        substituted = true; // T-2 evidence: the reticle redirect is final
    }

    // T-2 shot event for Reach. The unit-adjust transaction IS the engine's
    // projectile-builder call (HREK 0xD67EE0; the pinned retail module has
    // exactly one image caller, the projectile transaction at 0x4C303A), so
    // every invocation this hook serves is a firing invocation and one event
    // is published per invocation. Final origin/direction are read after the
    // body: post-substitution when a mod path produced the ray, otherwise the
    // engine's stock values. Reach consumes no prepared serial inside this
    // weapon transaction (F3), so the event carries the latest published
    // prepared serial - a bounded snapshot, never an equality claim.
    void PublishReachShotDiagnostic(int32_t unitIndex, const float* origin, const float* direction, bool substituted,
        bool firesFromCamera, bool unitAim)
    {
        uint32_t weapon = UINT32_MAX;
        uint8_t barrel = kTelemetryShotIndexUnknown;
        ReachReadFiringContextForTelemetry(weapon, barrel);
        PublishShotDiagnostic(GameTitle::HaloReach, g_reachCamera.generation.load(std::memory_order_acquire),
            static_cast<uint32_t>(unitIndex), weapon, kTelemetryShotIndexUnknown, barrel,
            /*predicted=*/false, substituted, firesFromCamera, unitAim, origin, direction);
    }

    __declspec(noinline) void __fastcall ReachUnitAdjustHook(int32_t unitIndex, float* origin, float* direction,
        const float* basisForward, const float* barrelOffset, const float* optionalCameraPoint, uint8_t firesFromCamera,
        uint8_t useUnitAim, uint8_t collision, uint32_t simulation)
    {
        // T-2 shot gate FIRST: one atomic load, before any diagnostic read.
        // The transaction below is gameplay and always runs; this only decides
        // whether the same invocation also publishes a shot event.
        const bool telemetryAccepting = Telemetry_WeaponEventsAccepting();
        g_reachCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            if (reinterpret_cast<uintptr_t>(_ReturnAddress()) == g_reachCamera.base + 0x4C303F &&
                ReachApplyBarrelAim(unitIndex, origin, direction, basisForward, collision, simulation))
            {
                // The committed muzzle barrel produced this final ray.
                if (telemetryAccepting)
                    PublishReachShotDiagnostic(
                        unitIndex, origin, direction, true, firesFromCamera != 0, useUnitAim != 0);
                __leave;
            }
            bool substituted = false;
            ReachUnitAdjustBody(unitIndex, origin, direction, basisForward, barrelOffset, optionalCameraPoint,
                firesFromCamera, useUnitAim, collision, simulation, substituted);
            if (telemetryAccepting)
                PublishReachShotDiagnostic(
                    unitIndex, origin, direction, substituted, firesFromCamera != 0, useUnitAim != 0);
        }
        __finally
        {
            g_reachCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    constinit Halo3VehicleDebounce g_reachVehicleFpDebounce;
    constinit ReachSeatRecenterLatch g_reachSeatRecenterLatch;
    constinit Halo3SeatYaw g_reachSeatYaw;
    constinit Halo3SeatPositionKey g_reachYawSeat;

    struct ReachHeadReference
    {
        uint32_t generation = 0;
        int32_t unitHandle = -1;
        int32_t directParent = -1;
        int32_t seatIndex = -1;
        ReachVehicleId identity = ReachVehicleId::Unknown;
        bool interpolateNodes = false;
        Halo3HeadSettleLatch settle{};
    };
    constinit ReachHeadReference g_reachHeadReference;

    // The seat is parented to the CAR. Reach's authored camera and
    // attachment markers ride the vehicle/occupant animation graphs - the sit
    // markers sway with the driving animations - so anchoring the eye to the
    // live marker dragged the view around the cabin while driving. Halo 3's
    // accepted design removed exactly this class of motion (a rigid seat
    // anchor, not animated markers). The marker's offset inside the rendered
    // vehicle-root frame is latched once per seat occupation; the camera then
    // rides the rigid root body, so hull physics reaches the head and
    // animation does not. Occupant motion re-enters only through the scaled
    // vehicle_bounce residual, exactly as in Halo 3.
    struct ReachSeatAnchorLatch
    {
        uint32_t generation = 0;
        int32_t unitHandle = -1;
        int32_t directParent = -1;
        int32_t seatIndex = -1;
        ReachVehicleId identity = ReachVehicleId::Unknown;
        bool interpolateNodes = false;
        bool valid = false;
        float anchorLocal[3]{};
    };
    constinit ReachSeatAnchorLatch g_reachSeatAnchorLatch;
    constinit std::atomic<uint32_t> g_reachVehicleAnchorFallbacks{0};

    // Engine-thread-owned payload. Publication/teardown coordination lives in
    // g_reachVehicleSeatPatchState; no foreign pointer crosses a frame.
    constinit ReachSeatLeasePayload g_reachNativeSeatLease;
    // Weather-control telemetry. A report is only usable if it can say whether
    // each control was actually asserted, so these are reported every window
    // rather than only on change.
    constinit std::atomic<uint32_t> g_reachRainSuppressedEyes{0};
    constinit std::atomic<uint32_t> g_reachFogSuppressedEyes{0};
    constinit std::atomic<uint32_t> g_reachWeatherWriteFailures{0};
    constinit std::atomic<uint32_t> g_reachFogFlagsUnresolved{0};
    constinit std::atomic<uint32_t> g_reachLightmapShadowSuppressedEyes{0};
    constinit std::atomic<uint32_t> g_reachLightmapShadowAlreadyDisabledEyes{0};
    constinit std::atomic<uint32_t> g_reachLightmapShadowWriteFailures{0};
    constinit std::atomic<uint32_t> g_reachLightmapShadowRestoreFailures{0};
    constinit std::atomic<uint8_t> g_reachLightmapShadowOutstandingValue{0};
    constinit std::atomic<bool> g_reachLightmapShadowRestoreOutstanding{false};
    constinit std::atomic<uint32_t> g_reachSsaoSuppressedEyeCalls[2]{};
    constinit std::atomic<uint32_t> g_reachSsaoPassthroughCalls{0};
    bool RestoreReachNativeWeaponIkBypass();
    bool RestoreReachLightmapShadowsControl();
    bool RestoreReachThirdPersonEffectSuppression();
    bool RestoreReachSkyParallaxNeutralization();
    void ReachMuzzleRetargetRestore();
    static_assert(std::atomic<int>::is_always_lock_free);
    static_assert(std::atomic<bool>::is_always_lock_free);
    static_assert(std::atomic<uint8_t>::is_always_lock_free);
    static_assert(std::atomic<uint32_t>::is_always_lock_free);
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
} // namespace
namespace reach_bridge
{
    // Published only after the matching eye was rendered and copied. The
    // release/acquire serial binds each pair of projection scales to the exact
    // OpenXR prepared frame that will submit those Reach eye textures.
    constinit std::atomic<float> g_reachRenderHalfFovX[2]{};
    constinit std::atomic<float> g_reachRenderHalfFovY[2]{};
    constinit std::atomic<uint64_t> g_reachRenderFovSerial[2]{};
} // namespace reach_bridge
namespace
{
    // Diagnostic: the engine's own zoom. The stock compact camera's vertical
    // FOV (+0x28) before the head transform replaces it, and the zoom level the
    // engine passes to its aim query (last call, -2 = never seen).
    constinit std::atomic<float> g_reachStockVerticalFov{0};
    constinit std::atomic<int32_t> g_reachEngineZoomLevel{-2};
    constinit std::atomic<uint64_t> g_reachEngineZoomMs{0};

    constinit ReachPlayerViewRenderFn g_reachOrigPlayerViewRender = nullptr;
    constinit ReachMainRenderViewFn g_reachOrigMainRenderView = nullptr;
    using ReachFpInterpolateFn = bool(__fastcall*)(int, int, int, BoneMatrix**, int*);
    constinit ReachFpInterpolateFn g_reachOrigFpInterpolate = nullptr;
    // Production first-person palette trampoline. ABI verified against the
    // pinned Reach image at 0x2B4EB0.
    using ReachFpPaletteFn = void(__fastcall*)(
        uint16_t, const BoneMatrix*, BoneMatrix*, uintptr_t, const BoneMatrix*, const int32_t*);
    constinit ReachFpPaletteFn g_reachOrigFpPalette = nullptr;
    constinit ReachFpCameraRebuildFn g_reachOrigFpCameraRebuild = nullptr;
    constinit ReachFpCameraUploadFn g_reachFpCameraUpload = nullptr;
    using ReachRenderSsaoFn = void(__fastcall*)(void*);
    constinit ReachRenderSsaoFn g_reachOrigRenderSsao = nullptr;
} // namespace
namespace reach_bridge
{
    constinit ReachHudDrawWidgetFn g_reachOrigHudDrawWidget = nullptr;
} // namespace reach_bridge
namespace
{
    // A runtime authored-target loss rejects parity for this exact title-module
    // generation. Do not churn through remove/reinstall loops or silently arm a
    // lesser mode; a fresh module generation is required for another proof.
    constinit std::atomic<uint32_t> g_reachChudParityFailedGeneration{0};

    static int SafeReadU32(const void* slot, uint32_t* value)
    {
        __try
        {
            *value = *reinterpret_cast<const volatile uint32_t*>(slot);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading a Reach u32 faulted");
            return 0;
        }
        return 1;
    }

    static int SafeReadPtr(const void* slot, uintptr_t* value)
    {
        __try
        {
            *value = *reinterpret_cast<const volatile uintptr_t*>(slot);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading a Reach pointer faulted");
            return 0;
        }
        return 1;
    }

    // Bind Reach's atmospheric-fog control from its own gate rather than by
    // name: `render_atmosphere_fog` is a debug COMMAND whose table slot points
    // into .text (see reach_render_logic.h). This decodes, out of the already
    // hash-pinned image, the helper call edge, the TLS-index load, the +0x168
    // slot offset, and the exact test/branch. Any changed opcode, index, offset,
    // mask or branch leaves only this one feature stock.
    bool ReachVerifyAtmosphereFogGate(uintptr_t base, size_t size, const uint32_t*& tlsIndex)
    {
        tlsIndex = nullptr;
        const auto fits = [size](uintptr_t rva, size_t bytes) { return rva < size && bytes <= size - rva; };
        if (!fits(kReachAtmosphereFogTlsIndexLoadRva, 6) || !fits(kReachAtmosphereFogSlotLoadRva, 6) ||
            !fits(kReachAtmosphereFogGateTestRva, 5) || !fits(kReachAtmosphereFogTlsIndexRva, sizeof(uint32_t)) ||
            !ReachVerifyRel32Call(base, kReachAtmosphereFogHelperCallRva, kReachAtmosphereFogHelperRva))
        {
            return false;
        }

        // mov edx, dword ptr [rip + rel32]  -> the render TLS index global
        const uint8_t* const load = reinterpret_cast<const uint8_t*>(base + kReachAtmosphereFogTlsIndexLoadRva);
        if (load[0] != 0x8B || load[1] != 0x15)
            return false;
        int32_t displacement = 0;
        memcpy(&displacement, load + 2, sizeof(displacement));
        const uintptr_t next = base + kReachAtmosphereFogTlsIndexLoadRva + 6;
        if (static_cast<uintptr_t>(static_cast<intptr_t>(next) + displacement) != base + kReachAtmosphereFogTlsIndexRva)
        {
            return false;
        }

        // mov r14d, 0x168   then   test byte ptr [rax], 4   then   je
        const uint8_t* const slot = reinterpret_cast<const uint8_t*>(base + kReachAtmosphereFogSlotLoadRva);
        uint32_t slotOffset = 0;
        memcpy(&slotOffset, slot + 2, sizeof(slotOffset));
        const uint8_t* const gate = reinterpret_cast<const uint8_t*>(base + kReachAtmosphereFogGateTestRva);
        if (slot[0] != 0x41 || slot[1] != 0xBE || slotOffset != kReachAtmosphereFogFlagsSlotOffset || gate[0] != 0xF6 ||
            gate[1] != 0x00 || gate[2] != kReachAtmosphereFogEnableMask || gate[3] != 0x74 ||
            kReachAtmosphereFogSkipJumpRva != kReachAtmosphereFogGateTestRva + 3)
        {
            return false;
        }

        const auto* const resolved = reinterpret_cast<const uint32_t*>(base + kReachAtmosphereFogTlsIndexRva);
        uint32_t index = 0;
        if (!SafeReadU32(resolved, &index) || index >= kReachRenderTlsIndexLimit)
        {
            return false;
        }
        tlsIndex = resolved;
        return true;
    }

    // Walk the engine's own path to the render flags byte: module TLS index ->
    // this thread's TLS block -> the render-globals pointer at +0x168. Three
    // loads, allocation-free, and only ever run on the render thread inside an
    // owned eye - which is the thread whose flags the atmosphere helper reads.
    uint8_t* ReachResolveAtmosphereFogFlags() noexcept
    {
        const uint32_t* const indexSlot = g_reachCamera.atmosphereFogTlsIndex;
        if (!indexSlot)
            return nullptr;
        __try
        {
            const uint32_t index = *indexSlot;
            if (index >= kReachRenderTlsIndexLimit)
                return nullptr;
            auto** slots = reinterpret_cast<void**>(__readgsqword(0x58));
            if (!slots)
                return nullptr;
            auto* block = reinterpret_cast<unsigned char*>(slots[index]);
            if (!block)
                return nullptr;
            return *reinterpret_cast<uint8_t**>(block + kReachAtmosphereFogFlagsSlotOffset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("resolving Reach's fog flags faulted");
            return nullptr;
        }
    }

    // Assert the player's weather choice at the top of every owned eye render.
    //
    // Deliberately an ASSERT rather than the save/suppress/restore scope patchy
    // fog uses. Patchy fog has to stay on for the title's own authored views and
    // is only wrong inside our stereo eyes; these two are a player preference
    // that should hold wherever the player is looking, so the control converges
    // on the setting instead - turning a setting back on writes the title's own
    // value back on the very next eye. That also makes it self-healing against
    // Reach's tag loader, which rewrites render globals on level load exactly as
    // it does the motion-blur ones.
    //
    // Failure is per-feature and never touches the render result: an unresolved
    // or unwritable control leaves that one effect stock and is counted for the
    // worker to report. It must not drop the eye.
    void ReachApplyWeatherControls() noexcept
    {
        if (uint8_t* const rain = g_reachCamera.renderRainSlot)
        {
            uint8_t current = 0;
            if (!SafeReadByte(rain, &current))
            {
                g_reachWeatherWriteFailures.fetch_add(1, std::memory_order_relaxed);
            }
            else if (!g_config.rain)
            {
                if (current != 0)
                {
                    // Only a value we did not write can define "stock".
                    if (!g_reachCamera.renderRainSuppressed)
                        g_reachCamera.renderRainOriginal = current;
                    if (SafeWriteByte(rain, 0))
                        g_reachCamera.renderRainSuppressed = true;
                    else
                        g_reachWeatherWriteFailures.fetch_add(1, std::memory_order_relaxed);
                }
                g_reachRainSuppressedEyes.fetch_add(1, std::memory_order_relaxed);
            }
            else if (g_reachCamera.renderRainSuppressed)
            {
                if (SafeWriteByte(rain, g_reachCamera.renderRainOriginal))
                    g_reachCamera.renderRainSuppressed = false;
                else
                    g_reachWeatherWriteFailures.fetch_add(1, std::memory_order_relaxed);
            }
        }

        if (!g_reachCamera.atmosphereFogTlsIndex)
            return;
        uint8_t* const fog = ReachResolveAtmosphereFogFlags();
        if (!fog)
        {
            // The render globals are allocated on demand; before they exist
            // there is no fog to suppress and nothing is wrong.
            g_reachFogFlagsUnresolved.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        uint8_t flags = 0;
        if (!SafeReadByte(fog, &flags))
        {
            g_reachWeatherWriteFailures.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (!g_config.atmospheric_fog)
        {
            if (flags & kReachAtmosphereFogEnableMask)
            {
                if (SafeWriteByte(fog, ReachAtmosphereFogSuppressedFlags(flags)))
                {
                    g_reachCamera.atmosphereFogSuppressed = true;
                }
                else
                {
                    g_reachWeatherWriteFailures.fetch_add(1, std::memory_order_relaxed);
                }
            }
            g_reachFogSuppressedEyes.fetch_add(1, std::memory_order_relaxed);
        }
        else if (g_reachCamera.atmosphereFogSuppressed)
        {
            if (SafeWriteByte(fog, ReachAtmosphereFogRestoredFlags(flags)))
                g_reachCamera.atmosphereFogSuppressed = false;
            else
                g_reachWeatherWriteFailures.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // Reach's own resolution, decoded from 0x2DA68A-0x2DA6EC. See
    // reach_render_logic.h for the derivation. Returns false if any link in
    // the chain cannot be read; the caller must treat that as a real failure,
    // never as "not a crosshair".
    // This runs for every CHUD widget draw - roughly ten thousand times a
    // second - so the five-level pointer walk below is cached. The class of a
    // given (definition, collection) pair is fixed tag data; it can only change
    // when the loaded tags change, which the camera generation tracks. Direct
    // mapped, fixed size, no allocation, thread_local so no synchronisation.
    struct ReachChudClassCacheEntry
    {
        uint32_t key = 0xFFFFFFFFu;
        uint32_t generation = 0;
        int8_t scriptingClass = -1;
        bool resolved = false;
    };
    constexpr size_t kReachChudClassCacheSize = 64;
    constinit thread_local ReachChudClassCacheEntry g_reachChudClassCache[kReachChudClassCacheSize];

    static bool ResolveReachChudCollectionClassUncached(uintptr_t base, const void* descriptor,
        unsigned int definitionIndex, uint8_t collectionIndex, int8_t& scriptingClass);

    static bool ResolveReachChudCollectionClass(
        uintptr_t base, const void* descriptor, unsigned int definitionIndex, int8_t& scriptingClass)
    {
        if (!base || !descriptor)
            return false;
        uint8_t collectionIndex = 0;
        if (!SafeReadByte(
                reinterpret_cast<const uint8_t*>(descriptor) + kReachChudDescriptorCollectionByte, &collectionIndex))
            return false;

        const uint32_t generation = g_reachCamera.generation.load(std::memory_order_relaxed);
        const uint32_t key = ((definitionIndex & 0xFFFFu) << 8) | collectionIndex;
        ReachChudClassCacheEntry& entry = g_reachChudClassCache[key % kReachChudClassCacheSize];
        if (entry.key == key && entry.generation == generation)
        {
            scriptingClass = entry.scriptingClass;
            return entry.resolved;
        }

        int8_t resolvedClass = -1;
        const bool ok =
            ResolveReachChudCollectionClassUncached(base, descriptor, definitionIndex, collectionIndex, resolvedClass);
        entry.key = key;
        entry.generation = generation;
        entry.scriptingClass = resolvedClass;
        entry.resolved = ok;
        scriptingClass = resolvedClass;
        return ok;
    }

    static bool ResolveReachChudCollectionClassUncached(uintptr_t base, const void* descriptor,
        unsigned int definitionIndex, uint8_t collectionIndex, int8_t& scriptingClass)
    {
        (void)descriptor;
        const auto pool = [base](uint32_t handle, uintptr_t& out) -> bool
        {
            const uintptr_t slot = base + kReachChudPoolTableRva + static_cast<uintptr_t>(handle >> 28) * 8;
            return SafeReadPtr(reinterpret_cast<const void*>(slot), &out) != 0;
        };

        uintptr_t globalPtr = 0;
        if (!SafeReadPtr(reinterpret_cast<const void*>(base + kReachChudDefinitionTableRva), &globalPtr) || !globalPtr)
            return false;

        uint32_t definitionHandle = 0;
        if (!SafeReadU32(
                reinterpret_cast<const void*>(globalPtr + static_cast<uintptr_t>(definitionIndex & 0xFFFFu) * 8 + 4),
                &definitionHandle))
            return false;

        uintptr_t definitionPool = 0;
        if (!pool(definitionHandle, definitionPool) || !definitionPool)
            return false;
        const uintptr_t chudDefinition = definitionPool + static_cast<uintptr_t>(definitionHandle) * 4;

        uint32_t collectionHandle = 0;
        if (!SafeReadU32(reinterpret_cast<const void*>(chudDefinition + 4), &collectionHandle))
            return false;

        uintptr_t collectionPool = 0;
        if (!pool(collectionHandle, collectionPool) || !collectionPool)
            return false;
        const uintptr_t collection =
            collectionPool + (static_cast<uintptr_t>(collectionHandle) +
                                 static_cast<uintptr_t>(collectionIndex) * kReachChudCollectionStride) *
                                 4;

        uint8_t rawClass = 0;
        if (!SafeReadByte(reinterpret_cast<const uint8_t*>(collection + kReachChudCollectionClassOffset), &rawClass))
            return false;
        scriptingClass = static_cast<int8_t>(rawClass);
        return true;
    }

    // Last art identity captured for Reach. Read by the compositor to decide
    // whether the authored reticle actually needs re-uploading this frame.
    constinit std::atomic<uint64_t> g_reachAuthoredCrosshairKey{0};

    bool ReachOwnsHudStereoTransaction()
    {
        if (TitleAdapter_GetActiveTitle() != GameTitle::HaloReach)
            return false;
        const uint32_t cameraGeneration = g_reachCamera.generation.load(std::memory_order_acquire);
        return cameraGeneration != 0 && TitleAdapter_GetGeneration(GameTitle::HaloReach) == cameraGeneration &&
               g_reachCamera.installed.load(std::memory_order_acquire) &&
               g_reachCamera.armed.load(std::memory_order_acquire) &&
               !g_reachCamera.teardownRequested.load(std::memory_order_acquire) &&
               g_enabled.load(std::memory_order_relaxed) && VR_IsStereoEnabled() && g_reachFpCameraEyeScope.active &&
               g_reachFpCameraEyeScope.generation == cameraGeneration;
    }

    // Moves Reach's CHUD sensor origin (record +0x9C, 52 bytes before the safe-frame slot) by a
    // HUD-pixel shift from its authored value. The authored value is learned once per slot and
    // re-learned whenever the memory no longer holds what this function last wrote (a reloaded tag).
    void ReachShiftSensorOrigin(float dx, float dy) noexcept
    {
        if (g_hudLayoutOwner.load(std::memory_order_acquire) != HudLayoutProfile::HaloReach)
            return;
        ReachSensorLearned* learned = g_reachSensorLearned;
        float authoredX = 0, authoredY = 0;
        bool wrote = false;
        for (int i = 0; i < kMaxSafeFrameHits; ++i)
        {
            const uintptr_t slot = g_safeFrameSlots[i].load(std::memory_order_relaxed);
            if (!slot)
            {
                learned[i] = {};
                continue;
            }
            __try
            {
                volatile float* origin = reinterpret_cast<volatile float*>(slot - 52);
                const float cx = origin[0], cy = origin[1];
                uint32_t cxBits, cyBits;
                memcpy(&cxBits, const_cast<const float*>(&cx), 4);
                memcpy(&cyBits, const_cast<const float*>(&cy), 4);
                ReachSensorLearned& entry = learned[i];
                if (entry.slot != slot || !entry.have || entry.wx != cxBits || entry.wy != cyBits)
                {
                    // Only learn an authored-looking origin (the record was verified with X = 88.0 at scan).
                    if (cxBits != 0x42B00000u || !std::isfinite(cy))
                        continue;
                    entry = {slot, true, cx, cy, 0, 0};
                }
                const float nx = entry.bx + dx, ny = entry.by + dy;
                if (!std::isfinite(nx) || !std::isfinite(ny))
                    continue;
                origin[0] = nx;
                origin[1] = ny;
                memcpy(&entry.wx, &nx, 4);
                memcpy(&entry.wy, &ny, 4);
                if (!wrote)
                {
                    authoredX = entry.bx;
                    authoredY = entry.by;
                    wrote = true;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                GUARD_COUNT("shifting Reach's motion-sensor origin faulted");
            }
        }
        static float lastDx = 1e30f, lastDy = 1e30f;
        if (wrote && (dx != lastDx || dy != lastDy))
        {
            lastDx = dx;
            lastDy = dy;
            LOG("reach sensor origin: shift=(%.1f,%.1f) authored=(%.1f,%.1f)", dx, dy, authoredX, authoredY);
        }
    }

#include "reach_hud_height.inl"

    // REACHHUD diagnostic counters. The CHUD hook is a hot hook, so it only
    // bumps atomics here; every log line is emitted by the 50 ms worker.
    // Purpose: the 2026-07-27 session lost the VR crosshair mid-level, and
    // headset observation ties it to on-screen objective text. These separate
    // the three candidate mechanisms - the engine stops drawing class-2
    // widgets, the collection descriptor stops being readable (which rejects
    // the transaction), or the art key churns and republishes different art -
    // instead of guessing between them.
    constinit std::atomic<uint64_t> g_reachChudLastClass2Ms{0};
    constinit std::atomic<uint32_t> g_reachChudClass2Draws{0};
    constinit std::atomic<uint32_t> g_reachChudUnreadable{0};
    constinit std::atomic<uint32_t> g_reachChudRejects{0};
    constinit std::atomic<uint32_t> g_reachChudRedirectUnavailable{0};
    constinit std::atomic<uint8_t> g_reachChudLastUnreadablePath{0};
    // Readable draws whose collection class was not 2, plus a histogram of
    // the classes actually seen. Closes the blind spot that made the
    // 2026-07-27 objective drought ambiguous.
    constexpr size_t kReachChudClassBuckets = 10;
    constinit std::atomic<uint32_t> g_reachChudOtherDraws{0};
    constinit std::array<std::atomic<uint32_t>, kReachChudClassBuckets> g_reachChudClassCounts{};

    void RejectReachChudParityForCurrentEye() noexcept
    {
        g_reachChudRejects.fetch_add(1, std::memory_order_relaxed);
        g_reachFpCameraEyeScope.chudParityFailed = true;
        g_reachChudParityFailedGeneration.store(g_reachCamera.generation, std::memory_order_release);
        g_reachCamera.armed.store(false, std::memory_order_release);
        g_reachCamera.teardownRequested.store(true, std::memory_order_release);
    }

    // A momentarily unavailable render-target redirect is NOT a fatal
    // transaction failure. It used to call the reject path above, which
    // disarms the core and tears down all six hooks - so a transient
    // resource state, or simply turning the crosshair off, killed VR for the
    // whole session. Both defects were observed in headset testing on
    // 2026-07-27, and it is the same shape as the two silent teardown paths
    // removed on 2026-07-26 (a feature failing must never take
    // down a working VR path).
    //
    // Consequence of the redirect being unavailable: this widget draws to the
    // eye instead of the offscreen target. With kill_reticle=1 its alpha is
    // already cleared, so nothing appears. The eye is marked so a partially
    // captured pair is never published - the compositor skips that frame and
    // retries, exactly like the accepted projection-view skip - and the lazy
    // capture entry creates whatever was missing on the next draw.
    void ReportReachRedirectUnavailable() noexcept
    {
        g_reachChudRedirectUnavailable.fetch_add(1, std::memory_order_relaxed);
        g_reachFpCameraEyeScope.chudParityFailed = true;
    }

    // Official HREK exposes the scripting-class byte directly to Reach's
    // five-argument chud_draw_widget transaction. Select only class 2 and
    // reuse Halo 3/ODST's authored-widget capture. No procedural reticle,
    // widget-name fallback, or mixed flat-crosshair mode exists.
    // ---- Reach rain: stop the volume swinging with the view ----------------
    // See kReachRainParticleRenderRva for the derivation. The renderer centres
    // the whole rain volume at position + forward * (size * 0.45), and in VR
    // that forward carries both head-look and hand aim, so the entire rain
    // field rotates with the player. Zeroing the forward for the duration of
    // this one call centres the volume on the camera instead, which keeps the
    // rain following the player's POSITION while decoupling it from rotation.
    //
    // Scoped save/substitute/restore around exactly one engine call on the
    // render thread, with __finally so an exception cannot leave the workspace
    // modified. This is not the banned "scoped camera write": that rule is
    // about splitting head-look between frames, and this touches a value the
    // rain renderer is the only documented consumer of during its own call.
    constinit const char* kReachRainRenderSig = "48 63 05 ?? ?? ?? ?? 44 0F 28 D2 85 C0 78 0D 48 8D 1D ?? ?? ?? ?? "
                                                "48 8B 1C C3 EB 02 33 DB";
    using ReachRainRenderFn = void(__fastcall*)(uintptr_t, uintptr_t, float);
    constinit ReachRainRenderFn g_reachOrigRainRender = nullptr;
    constinit std::atomic<uint32_t> g_reachRainDecoupled{0};
    constinit std::atomic<uint32_t> g_reachRainSkipped{0};

    // Resolve the workspace the rain renderer itself will use, the same way it
    // does: camera-stack depth indexes the camera-stack pointer array.
    uintptr_t ReachResolveTopCameraWorkspace()
    {
        const uintptr_t base = g_reachCamera.base;
        if (!base)
            return 0;
        __try
        {
            const int32_t depth = *reinterpret_cast<const int32_t*>(base + kReachCameraStackDepthRva);
            if (depth < 0 || depth > 64)
                return 0;
            const uintptr_t workspace = *reinterpret_cast<const uintptr_t*>(
                base + kReachCameraStackPointersRva + static_cast<uintptr_t>(depth) * sizeof(uintptr_t));
            if (workspace < base || workspace >= base + g_reachCamera.size)
                return 0;
            return workspace;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("resolving Reach's top camera workspace faulted");
            return 0;
        }
    }

    // Guarded save-and-zero of the workspace forward. Separate function so no
    // __try/__except is nested inside the detour's __try/__finally - that
    // nesting crashed the MSVC front end outright (CL.exe 0xC0000005).
    __declspec(noinline) bool ReachRainZeroForward(float* forward, float* saved)
    {
        __try
        {
            saved[0] = forward[0];
            saved[1] = forward[1];
            saved[2] = forward[2];
            if (!isfinite(saved[0]) || !isfinite(saved[1]) || !isfinite(saved[2]))
            {
                return false;
            }
            forward[0] = 0.0f;
            forward[1] = 0.0f;
            forward[2] = 0.0f;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("zeroing Reach's rain forward faulted");
            return false;
        }
    }

    __declspec(noinline) void ReachRainRestoreForward(float* forward, const float* saved)
    {
        __try
        {
            forward[0] = saved[0];
            forward[1] = saved[1];
            forward[2] = saved[2];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("restoring Reach's rain forward faulted");
        }
    }

    __declspec(noinline) void __fastcall ReachHudXformDetour(float x, float y, float z)
    {
        static std::atomic<bool> logged{false};
        g_reachCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            HudXformFn original = g_origReachHudXform;
            if (!original)
                return;
            const float b = GameBrightnessScale();
            if (b != 1.0f)
            {
                x *= b;
                y *= b;
            }
            original(x, y, z);
            if (!logged.exchange(true))
                LOG("Reach brightness: hook active (game_brightness %.2f)", b);
        }
        __finally
        {
            g_reachCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    __declspec(noinline) void __fastcall ReachRainRenderDetour(
        uintptr_t passthroughA, uintptr_t passthroughB, float intensity)
    {
        g_reachCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        float* forward = nullptr;
        float savedForward[3] = {0.0f, 0.0f, 0.0f};
        __try
        {
            ReachRainRenderFn original = g_reachOrigRainRender;
            if (!original)
                return;
            if (g_reachCamera.armed.load(std::memory_order_acquire) && g_enabled.load(std::memory_order_acquire))
            {
                const uintptr_t workspace = ReachResolveTopCameraWorkspace();
                if (workspace)
                {
                    float* candidate = reinterpret_cast<float*>(
                        workspace + kReachSecondaryCompactOffset + kReachCompactCameraForwardOffset);
                    if (ReachRainZeroForward(candidate, savedForward))
                    {
                        forward = candidate;
                        g_reachRainDecoupled.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                if (!forward)
                    g_reachRainSkipped.fetch_add(1, std::memory_order_relaxed);
            }
            original(passthroughA, passthroughB, intensity);
        }
        __finally
        {
            if (forward)
                ReachRainRestoreForward(forward, savedForward);
            g_reachCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    // ---- Reach second muzzle flash -----------------------------------------
    // Puts the face-stuck muzzle element onto the controller-held gun, beside
    // the one that already tracks it. See kReachEffectLocationResolverRva for
    // the full disassembly and why effect[0x50] is a sound discriminator.
    //
    // This deliberately does NOT hook the object marker query 0x00471C30 or
    // 0x0047044C. Those are genuinely shared with the projectile chain, and a
    // previous candidate that moved a shared marker consumer produced the
    // rejected "projectile origin too far right and rearward" result. This
    // redirects only the resolver's own first-person branch, for effects that
    // already declare a first-person weapon user.
    using ReachEffectLocationFn = void(__fastcall*)(void*, uintptr_t, void*);
    using ReachEffectFpMarkerFn = void(__fastcall*)(int, int, unsigned int, void*);
    constinit ReachEffectLocationFn g_reachOrigEffectLocation = nullptr;
    constinit ReachEffectFpMarkerFn g_reachEffectFpMarkerQuery = nullptr;
    constinit std::atomic<uint32_t> g_reachMuzzleRedirects{0};
    constinit std::atomic<uint32_t> g_reachMuzzleReadFailures{0};
    // Diagnostic, worker-logged only. The 2026-07-27 headset test proved the
    // hook installs and the first-person query decodes, yet the flash stayed on
    // the player's face - and nothing in the log could say which branch the
    // muzzle effect actually takes. These counters separate the three
    // possibilities that produce identical silence:
    //   noFpUser  - effect[0x50] low nibble is 0, so the engine itself treats
    //               this effect as world-only and the redirect can never fire
    //   fpNone    - a first-person user exists but the output index is 'none'
    //   alreadyFp - the location was already a first-person location
    // A bitmask of every distinct effect[0x50] seen is recorded too, because
    // "which values actually occur" is the fact that decides the real fix.
    // Identity-fallback repair. The stuck muzzle element is a location whose
    // marker lookup FAILED, so the resolver handed it the global identity
    // matrix and it renders at the origin of the view. Its sibling particle
    // systems on the SAME effect resolve their markers fine and land on the
    // controller-held gun. So: remember the last transform that resolved for
    // this effect, and when a sibling comes back as the identity fallback,
    // give it that transform instead. The systems line up.
    constinit const float* g_reachEffectIdentity = nullptr; // into the retained module
    constinit thread_local const void* g_reachLastGoodEffect = nullptr;
    constinit thread_local float g_reachLastGoodMatrix[kReachEffectMatrixFloats]{};
    constinit thread_local bool g_reachLastGoodValid = false;
    constinit std::atomic<uint32_t> g_reachMuzzleRepaired{0};
    constinit std::atomic<uint32_t> g_reachMuzzleIdentityNoSibling{0};

    constinit std::atomic<uint32_t> g_reachMuzzleBodyCopiesHidden{0};
    // Definition index of the player's own weapon effect, captured by the hot
    // hook for the retarget worker. 0xFFFFFFFF = nothing captured.
    constinit std::atomic<uint32_t> g_reachMuzzleCapturedDefIndex{0xFFFFFFFFu};

    // ---- the weapon anchor the muzzle flash needs --------------------------
    // Why the flash sits on the player's face: our palette work never moves the
    // game's skeleton. ReconstructVisiblePaletteSource builds the
    // controller-aligned weapon into g_fpPaletteScratch and hands THAT to the
    // renderer, and ReachRestoreFpLiveGraph puts the live graph back. So the
    // gun you see is composed from a scratch copy while the engine's real
    // weapon stays where it always was - at the player's body. Anything that
    // samples the real skeleton afterwards, like the effect system asking for
    // the muzzle marker, resolves against the un-moved weapon.
    //
    // That is also why four candidates aimed at effect LOCATIONS did nothing:
    // the location was resolving correctly the whole time, against a weapon we
    // had already put back.
    //
    // The palette already computes both transforms. Publishing them lets the
    // effect hook re-parent a marker from the stock weapon onto the controller
    // one exactly - full rigid transform, so there is no offset.
    struct ReachWeaponAnchor
    {
        std::atomic<uint32_t> seq{0}; // even = stable (single writer)
        std::atomic<uint32_t> generation{0};
        BoneMatrix stock{};
        BoneMatrix moved{};
    };
    constinit ReachWeaponAnchor g_reachWeaponAnchor;
    // Highest collection index seen per CHUD definition. HREK shows the HUD
    // muzzle-flash collection (`warning_flashes`) is always the LAST one, and
    // retail strips the name, so the index is the only handle. Learned rather
    // than hardcoded because the index differs per weapon (3 on the assault
    // rifle, 4 on magnum/dmr/sniper).
    constexpr size_t kReachChudMaxCollectionSlots = 256;
    constinit std::atomic<uint8_t> g_reachChudMaxCollection[kReachChudMaxCollectionSlots]{};
    constinit std::atomic<uint32_t> g_reachHudFlashHidden{0};
    constinit thread_local bool g_reachWeaponAnchorPending = false;
    constinit thread_local BoneMatrix g_reachWeaponAnchorMoved{};
    constinit std::atomic<uint32_t> g_reachLiveGraphWeaponWrites{0};
    constinit std::atomic<uint32_t> g_reachWorldWristWritesPrevented{0};

    void ReachPublishWeaponAnchor(const BoneMatrix& stock, const BoneMatrix& moved, uint32_t generation)
    {
        const uint32_t start = g_reachWeaponAnchor.seq.load(std::memory_order_relaxed);
        g_reachWeaponAnchor.seq.store(start + 1, std::memory_order_release);
        g_reachWeaponAnchor.stock = stock;
        g_reachWeaponAnchor.moved = moved;
        g_reachWeaponAnchor.generation.store(generation, std::memory_order_relaxed);
        g_reachWeaponAnchor.seq.store(start + 2, std::memory_order_release);
    }

    bool ReachReadWeaponAnchor(BoneMatrix& stock, BoneMatrix& moved)
    {
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const uint32_t before = g_reachWeaponAnchor.seq.load(std::memory_order_acquire);
            if (before == 0 || (before & 1u))
                continue;
            if (g_reachWeaponAnchor.generation.load(std::memory_order_relaxed) != g_reachCamera.generation)
            {
                return false;
            }
            stock = g_reachWeaponAnchor.stock;
            moved = g_reachWeaponAnchor.moved;
            if (g_reachWeaponAnchor.seq.load(std::memory_order_acquire) == before)
            {
                return true;
            }
        }
        return false;
    }

    // How near the stock weapon a marker must sit to count as belonging to it.
    // The AR's own muzzle_flash marker is 0.23 world units from the weapon
    // origin, and world units are ~3 m, so 0.5 covers any weapon's markers
    // while staying far inside the distance to another character.
    constexpr float kReachWeaponMarkerRadius = 0.5f;
    constinit std::atomic<uint32_t> g_reachMuzzleReparented{0};
    constinit std::atomic<uint32_t> g_reachMuzzleOutOfRange{0};
    constinit std::atomic<uint32_t> g_reachMuzzleNearestMilli{0xFFFFFFFFu};

    // R * v and R^T * v for the row-major 3x3 in a BoneMatrix.
    inline void ReachRotate(const float* r, const float* v, float* out)
    {
        out[0] = r[0] * v[0] + r[1] * v[1] + r[2] * v[2];
        out[1] = r[3] * v[0] + r[4] * v[1] + r[5] * v[2];
        out[2] = r[6] * v[0] + r[7] * v[1] + r[8] * v[2];
    }

    // Rebuild an effect matrix that sits on the stock weapon so it sits on the
    // controller-aligned weapon instead, preserving its exact local offset and
    // orientation. Guarded; leaves the matrix untouched on any fault or if it
    // is not on the stock weapon.
    __declspec(noinline) bool ReachReparentEffectMatrix(float* matrix, const BoneMatrix& stock, const BoneMatrix& moved)
    {
        __try
        {
            float* position = matrix + (kReachEffectMatrixPositionOffset / 4);
            float* rotation = matrix + 1; // BoneMatrix: scale, rotation[9]
            const float delta[3] = {position[0] - stock.translation[0], position[1] - stock.translation[1],
                position[2] - stock.translation[2]};
            const float distanceSquared = delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2];
            if (!isfinite(distanceSquared))
                return false;
            // Cheapest possible visibility into what the hook actually sees:
            // the closest any effect matrix came to the stock weapon. If the
            // repair never fires, this number says whether it was a near miss
            // or whether nothing is resolving near the weapon at all.
            const uint32_t milli = static_cast<uint32_t>(sqrtf(distanceSquared) * 1000.0f);
            uint32_t nearest = g_reachMuzzleNearestMilli.load(std::memory_order_relaxed);
            while (milli < nearest &&
                   !g_reachMuzzleNearestMilli.compare_exchange_weak(nearest, milli, std::memory_order_relaxed))
            {
            }
            if (distanceSquared > kReachWeaponMarkerRadius * kReachWeaponMarkerRadius)
            {
                g_reachMuzzleOutOfRange.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            // The flash's own orientation already comes from the engine's
            // weapon, which controller aim steers down the controller ray, so
            // it is kept exactly as resolved. Its POSITION keeps its offset in
            // the weapon's own frame: taken into stock-weapon space and rotated
            // back out with the controller-aligned weapon. The earlier
            // translation-only transfer left that offset pointing the way the
            // head-aligned stock weapon pointed, so shots visibly left the
            // wrong point of the gun, worse the more it was angled (headset
            // feedback).
            (void)rotation;
            float newPosition[3]{};
            weapon_frame_transfer::TransferPoint(
                stock.rotation, stock.translation, moved.rotation, moved.translation, position, newPosition);
            // Headset calibration (muzzle_height_m): the transfer above keeps
            // the authored marker offset in the gun's frame exactly, so
            // the origin lands on the barrel line but at the authored height,
            // which reads as a few inches low in VR. Lift it along the GUN's
            // own up axis, not world up, so it rolls with the weapon.
            //
            // This is the effect origin only. It runs after the projectile's
            // origin and direction are resolved, so the impact point, already
            // confirmed correct in the headset, is not touched. Reach-only in
            // practice: this hook is on Reach's effect-location resolver.
            const float lift = g_config.muzzle_height_m;
            if (lift != 0.0f)
            {
                // moved is a BoneMatrix, whose 3x3 is row-major with world =
                // R * local (see ReachRotate above). Halo authors weapon
                // frames +X forward / +Z up, which the HREK primary_trigger
                // record corroborates: direction +X along the barrel.
                const float localUp[3] = {0.0f, 0.0f, 1.0f};
                float worldUp[3];
                ReachRotate(moved.rotation, localUp, worldUp);
                const float length = sqrtf(worldUp[0] * worldUp[0] + worldUp[1] * worldUp[1] + worldUp[2] * worldUp[2]);
                if (isfinite(length) && length > 1.0e-4f)
                {
                    const float scale = lift * kReachWorldUnitsPerMeter / length;
                    for (int i = 0; i < 3; ++i)
                        newPosition[i] += worldUp[i] * scale;
                }
            }
            for (int i = 0; i < 3; ++i)
                if (!isfinite(newPosition[i]))
                    return false;
            position[0] = newPosition[0];
            position[1] = newPosition[1];
            position[2] = newPosition[2];
            g_reachMuzzleReparented.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reparenting a Reach effect matrix faulted");
            return false;
        }
    }

    constinit std::atomic<uint32_t> g_reachMuzzleWorldNoFpUser{0};
    constinit std::atomic<uint32_t> g_reachMuzzleWorldFpNone{0};
    constinit std::atomic<uint32_t> g_reachMuzzleAlreadyFp{0};
    constinit std::atomic<uint32_t> g_reachMuzzleFpByteLowMask{0};
    constinit std::atomic<uint32_t> g_reachMuzzleFpByteHighMask{0};

    // Guarded read of the two effect fields the decision needs.
    bool ReachReadEffectFpFields(
        const void* effect, unsigned char& fpUserByte, int& objectIndex, uint32_t& definitionIndex)
    {
        __try
        {
            const unsigned char* bytes = static_cast<const unsigned char*>(effect);
            fpUserByte = bytes[kReachEffectFpUserByteOffset];
            objectIndex = *reinterpret_cast<const int*>(bytes + kReachEffectObjectIndexOffset);
            definitionIndex = *reinterpret_cast<const uint32_t*>(bytes + 0x08);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading a Reach effect's first-person fields faulted");
            return false;
        }
    }

    __declspec(noinline) void __fastcall ReachEffectLocationDetour(
        void* effect, uintptr_t nodeDesignator, void* outMatrix)
    {
        g_reachCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            ReachEffectLocationFn original = g_reachOrigEffectLocation;
            if (!original)
                return;
            ReachEffectFpMarkerFn fpQuery = g_reachEffectFpMarkerQuery;
            if (!fpQuery || !effect || !outMatrix || !g_reachCamera.armed.load(std::memory_order_acquire) ||
                !g_enabled.load(std::memory_order_acquire))
            {
                original(effect, nodeDesignator, outMatrix);
                return;
            }
            unsigned char fpUserByte = 0;
            int objectIndex = 0;
            uint32_t effectDefinitionIndex = 0xFFFFFFFFu;
            if (!ReachReadEffectFpFields(effect, fpUserByte, objectIndex, effectDefinitionIndex))
            {
                g_reachMuzzleReadFailures.fetch_add(1, std::memory_order_relaxed);
                original(effect, nodeDesignator, outMatrix);
                return;
            }

            ReachEffectFpDecision decision =
                ReachDecideEffectLocation(fpUserByte, static_cast<unsigned int>(nodeDesignator));
            // DISABLED 2026-07-27 after a headset failure, kept for evidence.
            //
            // This redirect fired 26,576 times in 30 s of shooting and the
            // face-stuck muzzle flash did not move at all (REACHFX line).
            // That is conclusive: the flash the player sees
            // is not placed through this resolver's world path, so the redirect
            // buys nothing while relocating tens of thousands of unrelated
            // first-person weapon effect placements per half minute - exactly
            // the kind of unproven side effect that produced the rejected
            // projectile-origin result earlier in this project.
            //
            // HREK tag data says why the model was wrong: the muzzle flash is a
            // particle_system_definition_block_new gated by the static
            // effect_camera_modes enum (1 = only in first person, 2 = only in
            // third person) at particle-system element +0x1E, not an
            // effect_part placed at a marker location. The face-stuck element
            // is almost certainly the THIRD-PERSON set, which never renders
            // during real first-person play.
            //
            // The counters above stay live so the next candidate can still see
            // what this path does. There is no switch; the redirect is always
            // declined until new evidence shows the flash resolves here.
            decision.redirect = false;
            if (!decision.redirect)
            {
                // Record WHY, so one run distinguishes the three ways this can
                // decline. Atomics only - no logging on this path.
                const short designator = static_cast<short>(nodeDesignator & 0xFFFFu);
                if (designator < 0)
                {
                    g_reachMuzzleAlreadyFp.fetch_add(1, std::memory_order_relaxed);
                }
                else if ((fpUserByte & 0x0Fu) == 0u)
                {
                    g_reachMuzzleWorldNoFpUser.fetch_add(1, std::memory_order_relaxed);
                    g_reachMuzzleFpByteLowMask.fetch_or(1u << (fpUserByte & 0x0Fu), std::memory_order_relaxed);
                    g_reachMuzzleFpByteHighMask.fetch_or(1u << ((fpUserByte >> 4) & 0x0Fu), std::memory_order_relaxed);
                }
                else
                {
                    g_reachMuzzleWorldFpNone.fetch_add(1, std::memory_order_relaxed);
                    g_reachMuzzleFpByteHighMask.fetch_or(1u << ((fpUserByte >> 4) & 0x0Fu), std::memory_order_relaxed);
                    // The player's own weapon effect: hand its definition to
                    // the retarget worker. Relaxed store, latest wins.
                    g_reachMuzzleCapturedDefIndex.store(effectDefinitionIndex, std::memory_order_relaxed);
                }
                // The engine resolves the matrix HERE. Only after this call
                // does outMatrix hold a real marker transform; the previous
                // candidate re-parented beforehand, against uninitialised
                // memory, and the engine then overwrote it - which is exactly
                // why the probe came back with the nearest-approach sentinel
                // untouched and zero re-parents.
                original(effect, nodeDesignator, outMatrix);
                // ---- re-parent the face-stuck muzzle flash onto the gun --------
                // The resolver hands back a marker matrix built against the
                // engine's weapon, which our palette work never moved. If that
                // matrix sits on the stock weapon, rebuild it against the
                // controller-aligned weapon: express the marker in the stock
                // weapon's local frame, then re-apply that exact local offset and
                // orientation on the moved weapon. Rigid and exact - the marker
                // keeps its real position on the barrel, so there is no offset.
                //
                // An earlier candidate required both ownership and position after a
                // preserved log disproved the claim that proximity implied weapon
                // ownership. The gate worked exactly as intended, but its headset
                // result left the black world geometry unchanged. It is disabled
                // above and retained only as evidence; the next candidate starts
                // from the accepted behavior.
                {
                    BoneMatrix stockWeapon{};
                    BoneMatrix movedWeapon{};
                    float* resolved = static_cast<float*>(outMatrix);
                    if (g_reachCamera.armed.load(std::memory_order_acquire) &&
                        g_enabled.load(std::memory_order_acquire) && ReachReadWeaponAnchor(stockWeapon, movedWeapon))
                    {
                        ReachReparentEffectMatrix(resolved, stockWeapon, movedWeapon);
                    }
                }

                return;
            }
            // Exactly the call the engine's own first-person branch makes,
            // with the arguments it builds, so the world copy of this weapon
            // effect lands on the first-person weapon beside the one that
            // already tracks the controller.
            fpQuery(decision.userIndex, objectIndex, decision.markerIndex, outMatrix);
            g_reachMuzzleRedirects.fetch_add(1, std::memory_order_relaxed);
        }
        __finally
        {
            g_reachCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    // ---- Reach observer-camera re-parenting --------------------------------
    // Takes the rain, objective markers and character name tags OFF the hand.
    // They are not a HUD "layer" - they are every consumer of Reach's observer
    // camera, which our aim steering points down the controller ray while the
    // world renders from a private head copy. See
    // kReachRenderCameraFromObserverRva for the full derivation and the proven
    // destination layout.
    //
    // This does NOT apply head-look a second time. ReachApplyHeadLook consumes
    // one-shot state (g_needRecenter and g_reachCineCutRealign, both exchange())
    // so calling it again per frame would silently eat the manual recenter and
    // the accepted cutscene yaw realign. Instead this copies the camera the
    // world was ALREADY rendered from for this exact eye, which also guarantees
    // the markers can never disagree with the world they are drawn over.
    using ReachObserverCameraFn = void(__fastcall*)(void*, const void*);
    constinit ReachObserverCameraFn g_reachOrigObserverCamera = nullptr;
    constinit std::atomic<uint32_t> g_reachObserverCameraSiteHits[6]{};
    constinit std::atomic<uint32_t> g_reachObserverCameraCorrected[6]{};
    constinit std::atomic<uint32_t> g_reachObserverCameraUnknownSite{0};

    __declspec(noinline) void __fastcall ReachObserverCameraDetour(void* dst, const void* src)
    {
        const uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
        g_reachCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            ReachObserverCameraFn original = g_reachOrigObserverCamera;
            if (!original)
                return;
            // Always run the engine's own build first, unconditionally, on
            // every path. A skipped call leaves the destination camera
            // uninitialised; correcting it afterwards is purely additive.
            original(dst, src);

            const uintptr_t base = g_reachCamera.base;
            if (!dst || !base || returnAddress <= base)
                return;
            const int site = ReachClassifyObserverCameraReturn(returnAddress - base);
            if (site < 0)
            {
                g_reachObserverCameraUnknownSite.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            g_reachObserverCameraSiteHits[site].fetch_add(1, std::memory_order_relaxed);
            // The world render camera is the headset-accepted path and already
            // carries head-look. Touching it would double-apply and put the
            // accepted Reach 3D at risk, so it is never corrected.
            if (site == kReachObserverCameraWorldSite)
                return;
            if (!ReachOwnsHudStereoTransaction())
                return;
            const ReachFpCameraEyeScope& scope = g_reachFpCameraEyeScope;
            if (!scope.active || scope.generation != g_reachCamera.generation)
            {
                return;
            }
            // Destination and the cached head camera share the compact layout
            // exactly (proven by this function's own copy block), so this is a
            // straight three-field copy: position, forward, up. Field of view
            // and everything else the engine derived is left alone.
            const float* headPos = reinterpret_cast<const float*>(scope.compact + 0x00);
            const float* headFwd = reinterpret_cast<const float*>(scope.compact + 0x0C);
            const float* headUp = reinterpret_cast<const float*>(scope.compact + 0x18);
            for (int i = 0; i < 3; ++i)
            {
                if (!isfinite(headPos[i]) || !isfinite(headFwd[i]) || !isfinite(headUp[i]))
                {
                    return;
                }
            }
            unsigned char* out = static_cast<unsigned char*>(dst);
            memcpy(out + 0x00, headPos, sizeof(float) * 3);
            memcpy(out + 0x0C, headFwd, sizeof(float) * 3);
            memcpy(out + 0x18, headUp, sizeof(float) * 3);
            g_reachObserverCameraCorrected[site].fetch_add(1, std::memory_order_relaxed);
        }
        __finally
        {
            g_reachCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    __declspec(noinline) void __fastcall ReachHudDrawWidgetDetour(
        int userIndex, void* descriptor, unsigned int widgetIndex, unsigned int useAlternatePath, void* drawState)
    {
        bool captureStarted = false;
        const bool hideHud = g_reachScopeRendering || (VR_HudHidden() && ReachOwnsHudStereoTransaction());
        if (hideHud)
            ++hud_visibility::depth;
        const bool previousHeightRedirected = g_reachHudHeightRedirected;
        const int previousDrawElement = g_reachHudDrawElement;
        g_reachHudDrawElement = -1;
        g_reachCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            ReachHudDrawWidgetFn original = g_reachOrigHudDrawWidget;
            if (!original)
                return;
            if (g_reachScopeRendering)
            {
                // Retain native widget bookkeeping; suppress only its GPU draws.
                original(userIndex, descriptor, widgetIndex, useAlternatePath, drawState);
                return;
            }

            const bool ownsStereo = ReachOwnsHudStereoTransaction();
            const bool matchingEyeScope =
                g_reachFpCameraEyeScope.active && g_reachFpCameraEyeScope.generation == g_reachCamera.generation;
            if (matchingEyeScope && !ownsStereo)
            {
                // Ownership can be revoked by teardown or an explicit VR
                // disable while this stock draw call is already in flight.
                // Mark the eye so it cannot be copied, but still run the
                // engine's draw: skipping it is what corrupts state (see the
                // +0x2ED80C crash note below).
                g_reachFpCameraEyeScope.chudParityFailed = true;
                original(userIndex, descriptor, widgetIndex, useAlternatePath, drawState);
                return;
            }
            if (ownsStereo && g_scopeRenderActive.load(std::memory_order_acquire))
            {
                // The magnified world-only scope picture must not receive a
                // native CHUD widget, but the call still has to happen.
                // Redirect entry: hiding must not depend on crosshair=1.
                if (VR_BeginAuthoredReticleRedirect())
                    captureStarted = g_reachHudHeightRedirected = true;
                original(userIndex, descriptor, widgetIndex, useAlternatePath, drawState);
                return;
            }

            // descriptor+4 is a WIDGET INDEX, not the scripting class. The
            // class belongs to the owning collection, reached through
            // descriptor+3. Resolving it the way Reach does is the only way to
            // catch every piece of a crosshair: almost all drawn widgets
            // author their class as "undefined/use parent".
            uint8_t collectionIndexForFlash = 0;
            bool collectionIsFlash = false;
            int8_t resolvedClass = -1;
            const bool descriptorReadable = descriptor && ResolveReachChudCollectionClass(g_reachCamera.base,
                                                              descriptor, useAlternatePath, resolvedClass);
            const uint8_t rawClass = static_cast<uint8_t>(resolvedClass);
            if (descriptorReadable)
                g_reachHudDrawElement = hud_element::ElementForReachClass(resolvedClass);
            const bool isCrosshairClass = descriptorReadable && resolvedClass == kReachChudCrosshairScriptingClass;
            // Learn this definition's highest collection index, then treat that
            // collection as the HUD muzzle flash. Never the crosshair: the
            // crosshair collection is index 0 in every weapon HREK shows, so a
            // crosshair-class widget is excluded outright as a second guard.
            if (descriptorReadable && !isCrosshairClass &&
                SafeReadByte(reinterpret_cast<const uint8_t*>(descriptor) + kReachChudDescriptorCollectionByte,
                    &collectionIndexForFlash))
            {
                std::atomic<uint8_t>& slot =
                    g_reachChudMaxCollection[(useAlternatePath & 0xFFu) % kReachChudMaxCollectionSlots];
                uint8_t known = slot.load(std::memory_order_relaxed);
                while (collectionIndexForFlash > known &&
                       !slot.compare_exchange_weak(known, collectionIndexForFlash, std::memory_order_relaxed))
                {
                }
                // Require a settled maximum: only hide once this definition has
                // shown a collection ABOVE the ammo collections, so the first
                // frames cannot hide the bullet count while the max is still
                // being learned.
                const uint8_t settled = slot.load(std::memory_order_relaxed);
                collectionIsFlash = settled >= 3 && collectionIndexForFlash == settled;
            }
            if (ownsStereo && isCrosshairClass)
                g_reachFpCameraEyeScope.chudClass2Seen = true;
            // REACHHUD: atomics only, no logging on this hot hook.
            if (ownsStereo)
            {
                if (isCrosshairClass)
                {
                    g_reachChudClass2Draws.fetch_add(1, std::memory_order_relaxed);
                    g_reachChudLastClass2Ms.store(GetTickCount64(), std::memory_order_relaxed);
                }
                else if (!descriptorReadable)
                {
                    g_reachChudUnreadable.fetch_add(1, std::memory_order_relaxed);
                    g_reachChudLastUnreadablePath.store(
                        static_cast<uint8_t>(useAlternatePath & 0xFFu), std::memory_order_relaxed);
                }
                else
                {
                    // Readable, but its owning collection did not resolve to
                    // class 2. Previously counted by nothing, which left
                    // "0 unreadable, 0 rejects, no class-2" ambiguous between
                    // "Reach stopped drawing the crosshair" and "Reach is
                    // still drawing it and our class resolution stopped
                    // returning 2". The class comes from the owning
                    // COLLECTION via descriptor+3, and objective widgets can
                    // shift collection indices, so that second case is real.
                    // Record a class histogram (classes are the 9-value CHUD
                    // scripting enum; anything else buckets at the end).
                    g_reachChudOtherDraws.fetch_add(1, std::memory_order_relaxed);
                    const size_t bucket =
                        (resolvedClass >= 0 && resolvedClass < static_cast<int8_t>(kReachChudClassBuckets - 1))
                            ? static_cast<size_t>(resolvedClass)
                            : kReachChudClassBuckets - 1;
                    g_reachChudClassCounts[bucket].fetch_add(1, std::memory_order_relaxed);
                }
            }

            const int stereoEye = g_stereoEye.load(std::memory_order_relaxed);
            if (matchingEyeScope && g_reachFpCameraEyeScope.chudParityFailed && isCrosshairClass)
            {
                // Already-failed eye: hide the widget, but never skip the
                // call. Redirect entry for the same crosshair=0 reason.
                if (VR_BeginAuthoredReticleRedirect())
                    captureStarted = g_reachHudHeightRedirected = true;
                original(userIndex, descriptor, widgetIndex, useAlternatePath, drawState);
                return;
            }
            const ReachChudCrosshairAction action =
                ReachDecideChudCrosshairAction(ownsStereo, descriptorReadable, static_cast<int8_t>(rawClass),
                    g_config.crosshair, g_config.kill_reticle, stereoEye, g_config.right_eye_first);

            // NEVER skip the engine's own draw call. Halo 3 and ODST can hide
            // their crosshair by NOPing a class-2 visibility predicate, so the
            // widget function still runs in full and merely declines to draw.
            // Reach has no such predicate (proven), so the
            // previous approach returned early instead - skipping the call
            // outright. That corrupts engine state: a headset test crashed
            // haloreach.dll at +0x2ED80C, the first instruction of a function
            // whose very first act is "mov eax,[rcx+0x98]", i.e. it was handed
            // a null pointer that the skipped call was supposed to establish.
            // The identical fault address appeared in an earlier attempt at
            // this same hook, so this is the mechanism, not a coincidence.
            //
            // Instead, always run the original and redirect its pixels into
            // the offscreen authored-reticle target when they must not reach
            // the eye. Begin/End already save and restore render targets,
            // viewports and scissors, so the engine draws normally and simply
            // lands somewhere the player cannot see.

            const bool hideFromEye = action == ReachChudCrosshairAction::Suppress ||
                                     action == ReachChudCrosshairAction::CaptureAuthored ||
                                     action == ReachChudCrosshairAction::RejectTransaction;
            if (action == ReachChudCrosshairAction::RejectTransaction)
                RejectReachChudParityForCurrentEye();
            if (isCrosshairClass && hideFromEye)
            {
                // Fold this widget's identity in. Same weapon, same collection,
                // same widgets -> same key -> the art is unchanged and the
                // compositor can skip re-uploading it. This must cover every
                // path that actually redirects a crosshair widget, not just
                // the CaptureAuthored action: the art is captured through the
                // Suppress path as well, which is why the key stayed 0 and the
                // skip never engaged.
            }
            if (hideFromEye)
            {
                // Lazy-creating redirect that also ignores crosshair=0.
                // Two headset regressions came from this one call. The
                // PREPARED entry refuses unless every resource already
                // exists, and that refusal disarmed the core (armed
                // 01:29:20.548 -> stereo OFF .602). The plain capture entry
                // refuses when crosshair=0, and since Reach has no
                // visibility predicate and its alpha write is inert, that
                // refusal put the flat stock crosshair back on the eye for
                // players who asked for none.
                if (VR_BeginAuthoredReticleRedirect())
                {
                    captureStarted = g_reachHudHeightRedirected = true;
                    // Fold this widget's identity in AFTER the redirect
                    // begins, never before. Beginning a redirect on a new
                    // displayed frame is what CLEARS the capture surface and
                    // resets the per-frame key (see the serial check in
                    // BeginAuthoredReticleCaptureInternal). Folding first
                    // published a key describing content, and the clear then
                    // wiped the surface underneath it - so the key no longer
                    // described what the surface held, and the compositor
                    // could publish a blank or partial image over good art.
                    // That is the crosshair vanishing across a
                    // cinematic -> gameplay -> objective transition, where
                    // frames legitimately begin capturing in a different
                    // order. Folding here means the key is only ever built
                    // from widgets that actually reached the surface, on the
                    // surface that survived the clear.
                    if (isCrosshairClass)
                    {
                        ReachFpCameraEyeScope& scope = g_reachFpCameraEyeScope;
                        scope.captureKey = FoldAuthoredCrosshairKey(scope.captureKey, widgetIndex, useAlternatePath);
                        if (scope.captureDraws != 0xFFFFFFFFu)
                            ++scope.captureDraws;
                        g_authoredCrosshairKeyAccum = scope.captureKey;
                        g_authoredCrosshairKey.store(scope.captureKey, std::memory_order_release);
                        g_authoredCrosshairDraws.store(scope.captureDraws, std::memory_order_release);
                    }
                }
                else
                    ReportReachRedirectUnavailable();
            }
            original(userIndex, descriptor, widgetIndex, useAlternatePath, drawState);
        }
        __finally
        {
            if (hideHud)
                --hud_visibility::depth;
            g_reachHudHeightRedirected = previousHeightRedirected;
            g_reachHudDrawElement = previousDrawElement;
            if (captureStarted)
            {
                if (VR_EndPreparedAuthoredReticleCapture())
                {
                    g_reachFpCameraEyeScope.authoredCrosshairCaptured = true;
                    g_reachAuthoredCrosshairKey.store(g_reachFpCameraEyeScope.captureKey, std::memory_order_release);
                }
                else
                    // Same rule as the Begin side: restoring the render state
                    // failed for this widget, so this eye's art is incomplete
                    // and must not publish - but the camera core keeps running.
                    ReportReachRedirectUnavailable();
            }
            g_reachCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    constexpr size_t kReachFpLayoutCacheCapacity = 4;
    struct ReachFpLayoutCacheEntry
    {
        bool valid = false;
        bool invalidateNextPair = false;
        uint32_t generation = 0;
        int liveSourceCount = 0;
        int interpolationView = 0;
        int interpolationId = 0;
        int interpolationSlot = 0;
        uint16_t bodyTag = 0;
        uint64_t learnedPreparedSerial = 0;
        ReachFpBodyLayout layout{};
    };
    struct ReachFpPairScope
    {
        bool armed = false;
        bool showSeatedLegs = false;
        bool legPaletteValid = false;
        uint32_t generation = 0;
        uint64_t preparedSerial = 0;
        uint16_t legPaletteTag = 0;
        ReachFpLegPaletteKind legPaletteKind = ReachFpLegPaletteKind::None;
        // Persistent support grip: the owner trust frozen for this stereo pair
        // by the root FP interpolate invocation. `supportGripResolved` stays
        // false with the feature off, which keeps `targets.twoHandAimActive`
        // the only presentation input exactly as before (PG-off parity).
        bool supportGripAttached = false;
        bool supportGripResolved = false;
        bool fingerSampleExact = false;
        bool fingerWeaponEvidenceValid = false;
        bool secondaryWeaponPresent = false;
        bool supportFingerIsLeft = true;
        ControllerFingerInput supportFingerInput{};
        uint32_t fingerWeaponOwner = UINT32_MAX;
        uint32_t fingerPrimaryWeapon = UINT32_MAX;
        bool fingerLeftHanded = false;
        bool fingerHandAlignment = false;
        uint64_t fingerTrackingSerial = 0;
        uint64_t fingerTrackingEpoch = 0;
        int64_t fingerDisplayTimeNs = 0;
        FpExplicitPoseTargets targets{};
        ReachFpLayoutCacheEntry layouts[kReachFpLayoutCacheCapacity]{};
    };
    struct ReachFpLegPaletteObservation
    {
        bool valid = false;
        uint32_t generation = 0;
        uint64_t observedPreparedSerial = 0;
        uint16_t tag = 0;
        int nodeCount = 0;
    };
    struct ReachFpLegPaletteCacheEntry
    {
        bool valid = false;
        uint32_t generation = 0;
        uint64_t learnedPreparedSerial = 0;
        uint16_t tag = 0;
        ReachFpLegPaletteKind kind = ReachFpLegPaletteKind::None;
    };
    constinit thread_local ReachFpLayoutCacheEntry g_reachFpLayoutCache[kReachFpLayoutCacheCapacity];
    constinit thread_local ReachFpPairScope g_reachFpPairScope;
} // namespace
namespace reach_bridge
{
    constinit thread_local ReachFpInterpolationContext g_reachFpInterpolations[kReachFpTransactionCapacity];
} // namespace reach_bridge
namespace
{
    constinit thread_local ReachFpLegPaletteObservation g_reachFpLegPaletteObservations[kReachFpTransactionCapacity];
    constinit thread_local ReachFpLegPaletteCacheEntry g_reachFpLegPaletteCache;
    constinit thread_local uint64_t g_reachFpCaptureSerial = 0;
    constinit std::atomic<uint32_t> g_reachFpLegPaletteObservationCount{0};
    constinit std::atomic<uint32_t> g_reachFpLegPalettePreserved{0};
    constinit std::atomic<uint32_t> g_reachFpLegPaletteIdentityRejects{0};
    constinit std::atomic<uint32_t> g_reachFpLegPaletteBuildFailures{0};
    constinit std::atomic<uint64_t> g_reachFingerPoseApplied{0};
    constinit std::atomic<uint64_t> g_reachFingerPoseRefused{0};
    constinit std::atomic<uint64_t> g_reachFingerPoseWriteFailures{0};
    constinit std::atomic<uint64_t> g_reachFingerPoseRollbackFailures{0};

    constinit struct ReachFpStatus
    {
        std::atomic<uint64_t> key{0};
        std::atomic<uint32_t> generation{0};
        std::atomic<int> code{0};
        std::atomic<int> bodyCount{0};
        std::atomic<int> liveCount{0};
    } g_reachFpStatus;
    constinit std::atomic<uint64_t> g_reachFpLoggedStatusKey{0};
    constinit struct ReachFpCameraUploadStatus
    {
        std::atomic<uint64_t> preparedSerial{0};
        std::atomic<uint32_t> generation{0};
        std::atomic<uint32_t> eyeMask{0};
    } g_reachFpCameraUploadStatus;
    constinit std::atomic<uint32_t> g_reachFpCameraLoggedGeneration{0};

    constinit struct ReachOuterCameraCommitStatus
    {
        std::atomic<uint64_t> preparedSerial{0};
        std::atomic<uint32_t> generation{0};
        std::atomic<uint32_t> eyeMask{0};
    } g_reachOuterCameraCommitStatus;
    constinit std::atomic<uint32_t> g_reachOuterCameraCommitLoggedGeneration{0};

    void PublishReachOuterCameraCommitPair(uint32_t generation, uint64_t preparedSerial, uint32_t eyeMask) noexcept
    {
        g_reachOuterCameraCommitStatus.generation.store(generation, std::memory_order_relaxed);
        g_reachOuterCameraCommitStatus.eyeMask.store(eyeMask, std::memory_order_relaxed);
        g_reachOuterCameraCommitStatus.preparedSerial.store(preparedSerial, std::memory_order_release);
    }

    void ReachBeginFpPairScope(uint32_t generation, uint64_t preparedSerial, const ReachVrRenderSnapshot& tracking,
        const FpExplicitPoseTargets& targets, bool showSeatedLegs);
    void ReachEndFpPairScope();
    void ReachApplyFreeSupportFingerPose(
        uint16_t tag, int paletteCount, const ReachFpInterpolationContext& context, BoneMatrix* destination) noexcept;

    // Reach's apply_distortions pass divides motion_blur_max by
    // motion_blur_scale. Zeroing both controls creates 0/0 NaNs in the
    // translucent screen-space pass. Match accepted Halo 3/ODST comfort
    // behavior without invalid constants: retain/reassert a positive authored
    // scale and zero only the maximum at each exact normal stereo boundary.
    // Restore both authored values only after both detours are quiescent. These
    // render-hook helpers do no logging, allocation, locking, or scanning.
    bool ReachRestoreMotionBlurValues()
    {
        if (!g_reachCamera.motionBlurResolved || !g_reachCamera.motionBlurSuppressed)
        {
            return true;
        }
        for (MotionBlurVar& var : g_reachCamera.motionBlurVars)
        {
            if (!SafeWriteFloat(var.slot, var.original))
                return false;
        }
        g_reachCamera.motionBlurSuppressed = false;
        return true;
    }

    void ReachApplyMotionBlurSetting()
    {
        if (!g_reachCamera.motionBlurResolved)
            return;
        if (g_config.motion_blur)
        {
            ReachRestoreMotionBlurValues();
            return;
        }

        MotionBlurVar& scale = g_reachCamera.motionBlurVars[0];
        MotionBlurVar& maximum = g_reachCamera.motionBlurVars[1];
        float currentScale = 0.0f;
        float currentMaximum = 0.0f;
        if (!SafeReadFloat(scale.slot, &currentScale) || !SafeReadFloat(maximum.slot, &currentMaximum) ||
            !std::isfinite(currentMaximum) || currentMaximum < 0.0f)
        {
            return;
        }

        // Tags may reload either control. Preserve a newly authored usable
        // scale/max, but repair an unexpected zero/near-zero scale by
        // reasserting the positive value captured at install.
        if (ReachMotionBlurScaleUsable(currentScale))
            scale.original = currentScale;
        if (!ReachMotionBlurScaleUsable(scale.original))
            return;
        if (currentMaximum != 0.0f)
            maximum.original = currentMaximum;

        // Mark restoration pending before the first possible write. If either
        // protected write fails after a partial mutation, config re-enable or
        // quiescent teardown must still restore both authored values.
        g_reachCamera.motionBlurSuppressed = true;
        if (!SafeWriteFloat(scale.slot, scale.original))
            return;
        if (!SafeWriteFloat(maximum.slot, 0.0f))
            return;
    }

    // Reach draws patchy fog as screen-aligned translucent noise sheets. In
    // sequential stereo those sheets move opposite the tracked head even while
    // world fog and geometry are correct. The same exact owned-eye boundary is
    // also where the title's dynamic lightmap-shadow pass is suppressed. That
    // proven object-caster -> static-lightmap receiver boundary matches the
    // reported headset symptom and is isolated here as one candidate. Both
    // title controls are restored in __finally. Unclaimed, nested, screenshot,
    // and flat renders never enter this scope.
    // `transient` = this frame was skipped only because an earlier
    // lightmap restore is still outstanding (retried next frame); not a fault.
    bool ReachCallPlayerViewWithEyeScopedSuppressions(uintptr_t playerView, bool* transient = nullptr)
    {
        if (transient)
            *transient = false;
        uint8_t original = 0;
        bool suppressionWritten = false;
        bool callReturned = false;
        bool restoreSucceeded = true;
        uint8_t lightmapOriginal = 0;
        bool lightmapChanged = false;
        bool lightmapAlreadyDisabled = false;
        bool lightmapRestoreSucceeded = true;

        // A prior exceptional eye is never allowed to redefine zero as its
        // entry state. Recover our outstanding write before rendering again;
        // failure drops only this frame and the next frame will retry.
        if (g_reachLightmapShadowRestoreOutstanding.load(std::memory_order_acquire) &&
            !RestoreReachLightmapShadowsControl())
        {
            g_reachLightmapShadowRestoreFailures.fetch_add(1, std::memory_order_relaxed);
            if (transient)
                *transient = true;
            return false;
        }

        // Player weather preference, asserted before the eye renders. Kept out
        // of the __try/__finally below on purpose: it owns no scoped write, so
        // it has nothing to unwind, and its outcome must never influence
        // whether this eye is submitted.
        ReachApplyWeatherControls();

        __try
        {
            uint8_t* const lightmap = g_reachCamera.lightmapShadowsEnabled;
            if (!lightmap)
            {
                // Exact proof was unavailable; only this candidate stays stock.
            }
            else if (SafeReadByte(lightmap, &lightmapOriginal) && lightmapOriginal == 0)
            {
                lightmapAlreadyDisabled = true;
            }
            else if (lightmapOriginal == 1 && SafeWriteByte(lightmap, 0))
            {
                lightmapChanged = true;
                g_reachLightmapShadowOutstandingValue.store(lightmapOriginal, std::memory_order_relaxed);
                g_reachLightmapShadowRestoreOutstanding.store(true, std::memory_order_release);
            }
            else
            {
                // This feature alone stays stock for the eye. The worker-side
                // report makes the failure loud without logging in this hook.
                g_reachLightmapShadowWriteFailures.fetch_add(1, std::memory_order_relaxed);
            }
            uint8_t* const flags = g_reachCamera.patchyFogFlags;
            if (flags && SafeReadByte(flags, &original) &&
                SafeWriteByte(flags, ReachPatchyFogSuppressedFlags(original)))
            {
                suppressionWritten = true;
                g_reachOrigPlayerViewRender(playerView);
                callReturned = true;
            }
        }
        __finally
        {
            if (suppressionWritten)
            {
                uint8_t current = 0;
                uint8_t* const flags = g_reachCamera.patchyFogFlags;
                restoreSucceeded = flags && SafeReadByte(flags, &current) &&
                                   SafeWriteByte(flags, ReachPatchyFogRestoredFlags(current, original));
            }
            if (lightmapChanged)
            {
                uint8_t* const lightmap = g_reachCamera.lightmapShadowsEnabled;
                uint8_t restored = 0;
                lightmapRestoreSucceeded = lightmap && SafeWriteByte(lightmap, lightmapOriginal) &&
                                           SafeReadByte(lightmap, &restored) && restored == lightmapOriginal;
                if (lightmapRestoreSucceeded)
                {
                    g_reachLightmapShadowOutstandingValue.store(0, std::memory_order_relaxed);
                    g_reachLightmapShadowRestoreOutstanding.store(false, std::memory_order_release);
                }
                else
                {
                    g_reachLightmapShadowRestoreFailures.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        if (callReturned && restoreSucceeded && lightmapRestoreSucceeded)
        {
            if (lightmapChanged)
            {
                g_reachLightmapShadowSuppressedEyes.fetch_add(1, std::memory_order_relaxed);
            }
            else if (lightmapAlreadyDisabled)
            {
                g_reachLightmapShadowAlreadyDisabledEyes.fetch_add(1, std::memory_order_relaxed);
            }
        }
        return callReturned && restoreSucceeded && lightmapRestoreSucceeded;
    }

    uintptr_t ReachFpCameraWorkspaceIfLive(const void* view) noexcept
    {
        const ReachFpCameraEyeScope& scope = g_reachFpCameraEyeScope;
        const ReachOwnerScope& owner = g_reachOwnerScope;
        const uintptr_t base = g_reachCamera.base;
        if (!scope.active || g_reachNestedOuterSuppressed || !base ||
            !g_reachCamera.armed.load(std::memory_order_acquire) || scope.generation != g_reachCamera.generation ||
            scope.eye > 1 || g_stereoEye.load(std::memory_order_acquire) != static_cast<int>(scope.eye) ||
            !owner.active || owner.workspace != scope.workspace || owner.playerView != scope.playerView ||
            owner.preparedSerial != scope.preparedSerial || !owner.renderAccess || !owner.renderAccess->active ||
            owner.renderAccess->preparedSerial != scope.preparedSerial)
        {
            return 0;
        }

        const int32_t depth = *reinterpret_cast<const int32_t*>(base + kReachCameraStackDepthRva);
        if (depth < 0 || depth > 3)
            return 0;
        const uintptr_t topWorkspace = *reinterpret_cast<const uintptr_t*>(
            base + kReachCameraStackPointersRva + static_cast<uintptr_t>(depth) * sizeof(uintptr_t));
        const uintptr_t workspaceCallback = *reinterpret_cast<const uintptr_t*>(
            base + kReachFpCameraWorkspaceRva + kReachFpCameraWorkspaceCallbackOffset);
        const uintptr_t nestedWorkspace = SelectReachFpCameraNestedWorkspace(
            base, g_reachCamera.size, topWorkspace, workspaceCallback, reinterpret_cast<uintptr_t>(view));
        if (!nestedWorkspace || *reinterpret_cast<const uintptr_t*>(base + kReachActiveViewRva) != scope.playerView ||
            *reinterpret_cast<const uint32_t*>(base + kReachSelectedSpecializationRva) != 0)
        {
            return 0;
        }
        return nestedWorkspace;
    }

    void PublishReachFpCameraUpload(const ReachFpCameraEyeScope& scope) noexcept
    {
        const uint64_t serial = scope.preparedSerial;
        if (g_reachFpCameraUploadStatus.preparedSerial.load(std::memory_order_relaxed) != serial)
        {
            g_reachFpCameraUploadStatus.eyeMask.store(0, std::memory_order_relaxed);
            g_reachFpCameraUploadStatus.generation.store(scope.generation, std::memory_order_relaxed);
            g_reachFpCameraUploadStatus.preparedSerial.store(serial, std::memory_order_release);
        }
        g_reachFpCameraUploadStatus.eyeMask.fetch_or(uint32_t{1} << scope.eye, std::memory_order_release);
    }

    void ReachFpCameraRebuildBody(void* view, bool firstPersonEnabled)
    {
        ReachFpCameraRebuildFn original = g_reachOrigFpCameraRebuild;
        if (!original)
            return;

        // Preserve the title's complete first-person rebuild and FOV side
        // effects. During one exact admitted eye render, replace its final
        // compact camera and derived/projection pair with that eye's already
        // rebuilt world pair, then rerun Reach's own constant uploader. This is
        // the same last-writer transaction used by Halo 3 and ODST.
        original(view, firstPersonEnabled);
        if (!g_reachFpCameraUpload)
            return;

        ReachFpCameraEyeScope& scope = g_reachFpCameraEyeScope;
        const uintptr_t nestedWorkspace = ReachFpCameraWorkspaceIfLive(view);
        if (!nestedWorkspace)
            return;
        void* const compact = reinterpret_cast<void*>(nestedWorkspace);
        void* const derived = reinterpret_cast<void*>(nestedWorkspace + kReachSecondaryDerivedOffset);
        memcpy(compact, scope.compact, sizeof(scope.compact));
        memcpy(derived, scope.derived, sizeof(scope.derived));
        g_reachFpCameraUpload(compact, derived);
        PublishReachFpCameraUpload(scope);
    }

    __declspec(noinline) void __fastcall ReachFpCameraRebuildDetour(void* view, bool firstPersonEnabled)
    {
        g_reachCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            ReachFpCameraRebuildBody(view, firstPersonEnabled);
        }
        __finally
        {
            g_reachCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    bool ReachOwnsSsaoEyeTransaction(uintptr_t returnAddress) noexcept
    {
        if (!ReachOwnsHudStereoTransaction() || !g_reachCamera.ssaoIsolationActive.load(std::memory_order_acquire) ||
            g_reachNestedOuterSuppressed)
        {
            return false;
        }

        const uintptr_t base = g_reachCamera.base;
        const ReachFpCameraEyeScope& eyeScope = g_reachFpCameraEyeScope;
        const ReachOwnerScope& owner = g_reachOwnerScope;
        if (!base || returnAddress != base + kReachSsaoCallRva + 5 || eyeScope.eye > 1 ||
            g_stereoEye.load(std::memory_order_acquire) != static_cast<int>(eyeScope.eye) || !owner.active ||
            owner.workspace != eyeScope.workspace || owner.playerView != eyeScope.playerView ||
            owner.preparedSerial != eyeScope.preparedSerial || !owner.renderAccess || !owner.renderAccess->active ||
            owner.renderAccess->preparedSerial != eyeScope.preparedSerial || owner.cameraStackDepthBefore < -1 ||
            owner.cameraStackDepthBefore >= 3)
        {
            return false;
        }

        uint32_t depthRaw = 0;
        uintptr_t topWorkspace = 0;
        uintptr_t activeView = 0;
        uintptr_t workspaceCallback = 0;
        uint32_t specialization = 0;
        if (!SafeReadU32(reinterpret_cast<const void*>(base + kReachCameraStackDepthRva), &depthRaw) ||
            !SafeReadPtr(reinterpret_cast<const void*>(
                             base + kReachCameraStackPointersRva +
                             static_cast<uintptr_t>(owner.cameraStackDepthBefore + 1) * sizeof(uintptr_t)),
                &topWorkspace) ||
            !SafeReadPtr(reinterpret_cast<const void*>(base + kReachActiveViewRva), &activeView) ||
            !SafeReadPtr(
                reinterpret_cast<const void*>(owner.workspace + kReachRenderScopeSnapshotSize - sizeof(uintptr_t)),
                &workspaceCallback) ||
            !SafeReadU32(reinterpret_cast<const void*>(base + kReachSelectedSpecializationRva), &specialization))
        {
            return false;
        }

        const int32_t depth = static_cast<int32_t>(depthRaw);
        return depth == owner.cameraStackDepthBefore + 1 && depth >= 0 && depth <= 3 &&
               topWorkspace == owner.workspace && activeView == eyeScope.playerView &&
               workspaceCallback == base + kReachCameraStackCallbackRva && specialization == 0;
    }

    __declspec(noinline) void __fastcall ReachRenderSsaoDetour(void* ssaoDefinition)
    {
        const uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
        g_reachCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            if (ReachOwnsSsaoEyeTransaction(returnAddress))
            {
                const uint32_t eye = g_reachFpCameraEyeScope.eye;
                g_reachSsaoSuppressedEyeCalls[eye].fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                ReachRenderSsaoFn original = g_reachOrigRenderSsao;
                if (original)
                {
                    g_reachSsaoPassthroughCalls.fetch_add(1, std::memory_order_relaxed);
                    original(ssaoDefinition);
                }
            }
        }
        __finally
        {
            g_reachCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    // kReachSecondaryCompactOffset and the derived/render-bounds sub-block
    // offsets now live in reach_render_logic.h alongside the rest of the proven
    // workspace layout.
    // Player-view rollback: the camera-state block (+0x3B0) through the
    // last-window flag (+0xA30) inclusive.
    constexpr uintptr_t kReachPvSnapshotBegin = kReachPlayerViewCameraStateOffset;
    constexpr size_t kReachPvSnapshotBytes = kReachLastWindowFlagOffset + 1 - kReachPlayerViewCameraStateOffset;

    // Replicates Halo 3's proven ApplyHeadLook math (same engine family, same
    // world-axis convention: forward = (cos p cos y, cos p sin y, sin p), +Z up)
    // onto Reach's compact-camera offsets (pos +0x00, fwd +0x0C, up +0x18). It
    // shares the universal recenter/turn references so F-key recenter and stick
    // turn behave for Reach exactly as for Halo 3 and ODST.
    // Raised on the render thread when the probed cinematic state shows an
    // authored camera cut (ReachBuildHeadCullCamera); consumed exactly once by
    // ReachApplyHeadLook's realign branch in the same frame. The count feeds
    // the worker's confirmation log line.
    constinit std::atomic<bool> g_reachCineCutRealign{false};
    constinit std::atomic<uint32_t> g_reachCineCutCount{0};
    // Of those cuts, the ones the cinematic-globals stamp missed and only the
    // authored-camera discontinuity test caught. Reported alongside the total so
    // a headset log shows which detector is carrying the cutscene.
    constinit std::atomic<uint32_t> g_reachCineCutPoseCount{0};

    // A Reach shot cut is a discontinuous jump of the authored cinematic camera.
    // These bounds separate that jump from every legitimate authored move: the
    // fastest sustained cinematic whip pan is well under 200 deg/sec, which is
    // ~1.7 deg on a 120 Hz frame and ~7 deg even across a 33 ms hitch, and the
    // fastest camera-follow moves (vehicle chase shots) travel under 1 world
    // unit per frame under the same hitch. Anything past these is a cut, not
    // motion. Both are deliberately loose: a missed cut leaves the previous
    // shot's facing (today's bug), while a false one snaps the view mid-shot.
    constexpr float kReachCineCutYawRadians = 0.35f; // ~20 degrees
    constexpr float kReachCineCutJumpUnitsSq = 4.0f; // 2 world units

    bool ReachApplyHeadLook(unsigned char* cam, const ReachVrRenderSnapshot& tracking, const ReachVehicleFrame* vehicle,
        const ReachSeatLeaseKey* entryRecenterKey, ReachPendingSeatRecenter& pendingSeatRecenter)
    {
        pendingSeatRecenter = {};
        float q[4] = {tracking.headOrientation[0], tracking.headOrientation[1], tracking.headOrientation[2],
            tracking.headOrientation[3]};
        const float hpos[3] = {tracking.headPosition[0], tracking.headPosition[1], tracking.headPosition[2]};
        float qLengthSquared = 0.0f;
        for (float component : q)
        {
            if (!isfinite(component))
                return false;
            qLengthSquared += component * component;
        }
        for (float component : hpos)
            if (!isfinite(component))
                return false;
        if (!isfinite(qLengthSquared) || qLengthSquared <= 1e-8f)
            return false;
        const float qLength = sqrtf(qLengthSquared);
        for (float& component : q)
            component /= qLength;
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        const float hfx = -2.0f * (w * y + x * z);
        const float hfy = 2.0f * (w * x - y * z);
        const float hfz = -(1.0f - 2.0f * (x * x + y * y));
        const float hy = atan2f(hfx, -hfz);
        const float hp = asinf(Clamp(hfy, -1.0f, 1.0f));

        const float hux = 2.0f * (x * y - w * z);
        const float huy = 1.0f - 2.0f * (x * x + z * z);
        const float huz = 2.0f * (y * z + w * x);
        float hrx = -hfz, hrz = hfx;
        float hrLen = sqrtf(hrx * hrx + hrz * hrz);
        if (hrLen < 1e-4f)
            hrLen = 1e-4f;
        hrx /= hrLen;
        hrz /= hrLen;
        const float hnux = -hfy * hrz;
        const float hnuy = hrLen;
        const float hnuz = hfy * hrx;
        const float headRoll = atan2f(hux * hrx + huz * hrz, hux * hnux + huy * hnuy + huz * hnuz);

        float* pos = reinterpret_cast<float*>(cam + 0x00);
        float* fwd = reinterpret_cast<float*>(cam + 0x0C);
        float* up = reinterpret_cast<float*>(cam + 0x18);

        bool vehicleEntryRecenter = false;
        float vehicleEntryYaw = 0.0f;
        if (vehicle && vehicle->active && vehicle->entryForwardValid && entryRecenterKey &&
            ReachSeatLeaseKeyValid(*entryRecenterKey))
        {
            vehicleEntryRecenter = ReachVehicleEntryYaw(vehicle->entryForward, vehicleEntryYaw);
        }

        // An authored camera cut realigns yaw only, exactly like Halo 3/ODST
        // at their scene/shot IDs: the current physical head-forward maps to
        // the new shot's authored facing. A manual recenter additionally
        // rebaselines the lean/position reference; a cut must not, or leaning
        // would snap mid-cinematic.
        // An exact-seat stage must not acquire global one-shot requests before
        // the outer transaction commits. A projection/serial/camera rejection
        // leaves them untouched; an admitted seat reset coalesces them at its
        // commit because it performs the complete action.
        const bool manualRecenter = !vehicleEntryRecenter && g_needRecenter.exchange(false, std::memory_order_acq_rel);
        const bool cutRealign =
            !vehicleEntryRecenter && g_reachCineCutRealign.exchange(false, std::memory_order_acq_rel);
        float effectiveGameYawRef = g_gameYawRef;
        float effectiveHeadYawRef = g_headYawRef;
        float effectiveHeadPosRef[3]{g_headPosRef[0], g_headPosRef[1], g_headPosRef[2]};
        if (vehicleEntryRecenter)
        {
            // Build this first vehicle frame from the new exact-seat
            // references without publishing them yet. Projection, prepared
            // serial and outer-camera commit can still reject the transaction;
            // only the caller's admitted outer commit may consume the latch or
            // request a new OpenXR centre.
            effectiveGameYawRef = vehicleEntryYaw;
            effectiveHeadYawRef = hy;
            memcpy(effectiveHeadPosRef, hpos, sizeof(effectiveHeadPosRef));
            pendingSeatRecenter.valid = true;
            pendingSeatRecenter.key = *entryRecenterKey;
            pendingSeatRecenter.gameYaw = vehicleEntryYaw;
            pendingSeatRecenter.headYaw = hy;
            memcpy(pendingSeatRecenter.headPosition, hpos, sizeof(pendingSeatRecenter.headPosition));
        }
        else if (manualRecenter || cutRealign)
        {
            g_gameYawRef = atan2f(fwd[1], fwd[0]);
            g_headYawRef = hy;
            NoteYawReferenceLatched(GameTitle::HaloReach, g_reachCamera.generation.load(),
                manualRecenter ? "recenter" : "cut", g_gameYawRef, g_headYawRef, true);
            if (manualRecenter)
            {
                memcpy(g_headPosRef, hpos, sizeof(g_headPosRef));
                VR_NotePositionRecenter(); // the cabin fade re-captures its seated eye
                g_needPosRecenter.store(false, std::memory_order_release);
            }
        }
        else if (g_needPosRecenter.exchange(false))
        {
            memcpy(g_headPosRef, hpos, sizeof(g_headPosRef));
            VR_NotePositionRecenter(); // the cabin fade re-captures its seated eye
        }
        NoteSharedYawFrame(GameTitle::HaloReach, g_reachCamera.generation.load());
        if (!vehicleEntryRecenter)
        {
            effectiveGameYawRef = g_gameYawRef;
            effectiveHeadYawRef = g_headYawRef;
            memcpy(effectiveHeadPosRef, g_headPosRef, sizeof(effectiveHeadPosRef));
        }

        const float trackedYawDelta = g_yawSign.load() * WrapPi(hy - effectiveHeadYawRef);
        const float gy = effectiveGameYawRef + trackedYawDelta;
        const float gp = Clamp(g_pitchSign.load() * hp + g_pitchTrim.load(), -1.5f, 1.5f);
        const float cgp = cosf(gp), sgp = sinf(gp), cgy = cosf(gy), sgy = sinf(gy);
        float hullYaw = 0.0f, hullPitch = 0.0f, followBasis[9];
        const bool rotationFollows = Halo3ReadRollStableFollow(hullYaw, hullPitch) &&
                                     Halo3ComposeFollowBasisWithRoll(hullYaw, hullPitch, effectiveGameYawRef,
                                         trackedYawDelta, gp, headRoll, followBasis);
        if (rotationFollows)
        {
            memcpy(fwd, followBasis, sizeof(float) * 3);
            if (g_writeUp.load())
                memcpy(up, followBasis + 6, sizeof(float) * 3);
        }
        else
        {
            fwd[0] = cgp * cgy;
            fwd[1] = cgp * sgy;
            fwd[2] = sgp;
            if (g_writeUp.load())
            {
                const float cr = cosf(headRoll), sr = sinf(headRoll);
                up[0] = (-sgp * cgy) * cr + sgy * sr;
                up[1] = (-sgp * sgy) * cr - cgy * sr;
                up[2] = cgp * cr;
            }
        }
        ApplyCachedPhysicalCrouch(GameTitle::HaloReach, pos,
            -Clamp((hpos[1] - effectiveHeadPosRef[1]) * kReachWorldUnitsPerMeter, -1.5f, 1.5f), !vehicleEntryRecenter);
        Roomscale_Camera(GameTitle::HaloReach,
            !vehicleEntryRecenter && Game_RoomscaleCameraAllowed(GameTitle::HaloReach), pos, hpos, q, fwd,
            effectiveHeadPosRef, kReachWorldUnitsPerMeter);
        if (!vehicleEntryRecenter)
            memcpy(g_headPosRef, effectiveHeadPosRef, sizeof(g_headPosRef));
        if (g_positional.load())
        {
            const float dx = hpos[0] - effectiveHeadPosRef[0];
            const float dy = hpos[1] - effectiveHeadPosRef[1];
            const float dz = hpos[2] - effectiveHeadPosRef[2];
            float hlen = sqrtf(hfx * hfx + hfz * hfz);
            if (hlen < 1e-4f)
                hlen = 1e-4f;
            const float hfhx = hfx / hlen, hfhz = hfz / hlen;
            const float fwdComp = dx * hfhx + dz * hfhz;
            const float rightComp = dx * (-hfhz) + dz * hfhx;
            const float s = kReachWorldUnitsPerMeter;
            float ox = 0.0f, oy = 0.0f, oz = 0.0f;
            float positionBasis[9]{};
            const float positionLocal[3] = {fwdComp, -rightComp, dy};
            float positionWorld[3]{};
            const bool positionFollows = rotationFollows &&
                                         Halo3ComposeFollowBasisWithRoll(hullYaw, hullPitch, effectiveGameYawRef,
                                             trackedYawDelta, 0.0f, 0.0f, positionBasis) &&
                                         Halo3TransformBasisVector(positionBasis, positionLocal, positionWorld);
            if (positionFollows)
            {
                ox = positionWorld[0] * s;
                oy = positionWorld[1] * s;
                oz = positionWorld[2] * s;
            }
            else
            {
                ox = (cgy * fwdComp + sgy * rightComp) * s;
                oy = (sgy * fwdComp - cgy * rightComp) * s;
                oz = dy * s;
            }
            ox = Clamp(ox, -1.5f, 1.5f);
            oy = Clamp(oy, -1.5f, 1.5f);
            oz = Clamp(oz, -1.5f, 1.5f);
            pos[0] += ox;
            pos[1] += oy;
            pos[2] += oz;
        }
        return true;
    }

    bool ReachCommitPendingSeatRecenter(ReachPendingSeatRecenter& pending)
    {
        if (!pending.valid || !ReachSeatLeaseKeyValid(pending.key) || !isfinite(pending.gameYaw) ||
            !isfinite(pending.headYaw))
        {
            return false;
        }
        for (float component : pending.headPosition)
            if (!isfinite(component))
                return false;
        if (!g_reachSeatRecenterLatch.Commit(pending.key))
            return false;

        // The admitted head-centre camera already used these exact local
        // values. Publish them only now, after the outer camera commit can no
        // longer return to stock, then hand the OpenXR screen-centre request to
        // Present. The caller publishes the committed pair to input alongside
        // every other successfully committed Reach camera frame.
        g_gameYawRef = pending.gameYaw;
        g_headYawRef = pending.headYaw;
        NoteYawReferenceLatched(
            GameTitle::HaloReach, g_reachCamera.generation.load(), "seat", g_gameYawRef, g_headYawRef, true);
        memcpy(g_headPosRef, pending.headPosition, sizeof(g_headPosRef));
        VR_NotePositionRecenter(); // the cabin fade re-captures its seated eye
        g_needRecenter.exchange(false, std::memory_order_acq_rel);
        g_reachCineCutRealign.exchange(false, std::memory_order_acq_rel);
        // Do not clear a position-only token here: it may have been published
        // after this stage captured its headset pose. If present, the next
        // committed frame consumes it without repeating yaw/OpenXR recenter.
        VR_RequestRecenter();
        g_reachVehicleSeatRecenters.fetch_add(1, std::memory_order_relaxed);
        pending = {};
        return true;
    }

    // Snapshot both eyes once at the outer, pre-visibility boundary. Reach
    // rasterizes a symmetric fixed-aspect cover for each asymmetric OpenXR eye,
    // so the binocular cull is built from those actual widened raster corners
    // plus relative eye cant. The inner loop consumes the same snapshot instead
    // of re-reading a potentially different xrLocateViews result.
    bool ReachCollectEyeInputs(const ReachObservedRect& renderBounds, const ReachVrRenderSnapshot& tracking,
        ReachEyeRenderInput (&inputs)[2], ReachSymmetricFovCover& cullCover)
    {
        const int renderWidth = static_cast<int>(renderBounds.x1) - renderBounds.x0;
        const int renderHeight = static_cast<int>(renderBounds.y1) - renderBounds.y0;
        if (renderWidth <= 0 || renderHeight <= 0)
            return false;

        std::array<ReachEyeCullFrustum, 2> cullFrusta{};
        for (int eye = 0; eye < 2; ++eye)
        {
            const ReachVrEyeSnapshot& eyeSnapshot = tracking.eyes[eye];
            memcpy(inputs[eye].position, eyeSnapshot.position, sizeof(inputs[eye].position));
            for (float component : inputs[eye].position)
                if (!isfinite(component))
                    return false;

            ReachEyeCullFrustum frustum{};
            frustum.angleLeft = eyeSnapshot.fov[0];
            frustum.angleRight = eyeSnapshot.fov[1];
            frustum.angleUp = eyeSnapshot.fov[2];
            frustum.angleDown = eyeSnapshot.fov[3];
            frustum.relativeOrientation = {eyeSnapshot.orientation[0], eyeSnapshot.orientation[1],
                eyeSnapshot.orientation[2], eyeSnapshot.orientation[3]};
            ApplyCutsceneTheaterEyeTransform(VR_IsCutsceneTheaterActive(), g_config.cutscene_theater_depth,
                inputs[eye].position, frustum.relativeOrientation.data());
            inputs[eye].frustum = frustum;
            inputs[eye].rasterCover =
                SelectReachSymmetricFovCover(frustum.angleLeft, frustum.angleRight, frustum.angleUp, frustum.angleDown,
                    static_cast<uint32_t>(renderWidth), static_cast<uint32_t>(renderHeight));
            if (!inputs[eye].rasterCover.valid)
                return false;
            if (!BuildReachSymmetricRasterCullFrustum(inputs[eye].rasterCover, inputs[eye].frustum.relativeOrientation,
                    static_cast<uint32_t>(renderWidth), static_cast<uint32_t>(renderHeight), cullFrusta[eye]))
            {
                return false;
            }
        }

        cullCover = SelectReachStereoCullFovCover(
            cullFrusta, static_cast<uint32_t>(renderWidth), static_cast<uint32_t>(renderHeight));
        return cullCover.valid;
    }

    // ---- Reach cinematic-state probe (log-only, fail-open) -----------------
    // Evidence (2026-07-27, pinned haloreach.dll): Reach registers its
    // cutscene game state under the official names "cinematic globals"
    // (0x40 bytes) and "cinematic globals non deterministic" (0x10 bytes) at
    // exactly one registration site, and that site caches a per-engine-thread
    // pointer to each member inside the module's TLS block - the same design
    // HREK's exported symbol __tls_set_g_cinematic_globals_allocator names.
    // The signature below matches the registration's instruction stream; the
    // module TLS-index location, the verifying name string, and both TLS cache
    // slots are DECODED from the matched bytes, never hardcoded. The probe
    // READS and LOGS only - no engine write, no VR behavior change. Which
    // dwords mean in_progress / scene / shot is deliberately NOT assumed; the
    // first headset cutscene run assigns them from the log. A missing or
    // ambiguous match logs once and leaves the probe off.
    constexpr size_t kReachCineMemberADwords = 0x40 / 4;
    constexpr size_t kReachCineMemberBDwords = 0x10 / 4;
    constinit const char* kReachCineRegistrationSig = "65 48 8B 0C 25 58 00 00 00 " // mov rcx, gs:[0x58]
                                                      "4C 8D 35 ?? ?? ?? ?? "       // lea r14, [member-offset table]
                                                      "8B 15 ?? ?? ?? ?? "          // mov edx, [module TLS index] (+18)
                                                      "45 8D 4D 10 "
                                                      "89 05 ?? ?? ?? ?? "
                                                      "45 33 C0 "
                                                      "48 98 "
                                                      "4C 89 6C 24 38 "
                                                      "48 8B 1C D1 "          // mov rbx, [rcx+rdx*8]  (TLS block)
                                                      "48 8D 15 ?? ?? ?? ?? " // lea rdx, [verify name] (+49)
                                                      "4C 89 6C 24 30 "
                                                      "41 BF 28 00 00 00 "
                                                      "BD ?? ?? 00 00 " // mov ebp, state-buffer base offset
                                                      "BF ?? ?? 00 00 " // mov edi, member A TLS slot (+70)
                                                      "41 8B 0C 1F "
                                                      "89 0D ?? ?? ?? ?? "
                                                      "48 8D 0C 40 "
                                                      "48 03 C9 "
                                                      "41 8B 84 CE ?? ?? 00 00 " // mov eax, [r14+rcx*8+table]
                                                      "8D 4E C4 "
                                                      "48 03 44 2B 20 " // add rax, [rbx+rbp+0x20]
                                                      "48 89 04 1F";    // mov [rdi+rbx], rax  (cache member A)
    // Same function, a few instructions later: mov ecx, imm32 carrying the
    // member B TLS slot, immediately followed by the member B cache store
    // mov [rcx+rbx], rax. Searched only inside a bounded tail window.
    constinit const char* kReachCineSlotBSig = "B9 ?? ?? ?? ?? 48 89 04 19";

    // ---- Reach native pause flag -------------------------------------------
    // See kReachNativePauseFlagRva in reach_render_logic.h for how this was
    // identified (live differential + code-reference filter + a 10 Hz watch of
    // a five-second pause cadence). The signature below is the runtime binding:
    // the flag's own address is DECODED from the matched store's disp32, never
    // hardcoded, exactly like Halo 3's LocateNativePauseFlag and ODST's owner
    // proof. The store alone is ambiguous (39 sites); the TLS-member fetch and
    // the 0x200 state-bit set in front of it are what make it unique.
    constinit const char* kReachNativePauseOwnerSig = "8B 15 ?? ?? ?? ?? "          // mov edx, [module TLS index]
                                                      "65 48 8B 04 25 58 00 00 00 " // mov rax, gs:[0x58]
                                                      "B9 A0 00 00 00 "             // mov ecx, 0xA0  (member slot)
                                                      "48 8B 04 D0 "                // mov rax, [rax+rdx*8]
                                                      "48 8B 14 08 "                // mov rdx, [rax+rcx]
                                                      "B8 00 02 00 00 "             // mov eax, 0x200
                                                      "66 42 09 04 32 "             // or word ptr [rdx+r14], ax
                                                      "44 88 2D ?? ?? ?? ??";       // mov byte ptr [rip+d32], r13b
} // namespace
namespace reach_bridge
{
    // 0 = not located, otherwise the live address of Reach's pause byte.
    constinit std::atomic<uintptr_t> g_reachNativePauseFlag{0};
    // Cached last read, so the XInput path does not repeat a guarded engine
    // read: -1 unknown, 0 running, 1 paused. Published by the Present tick.
    constinit std::atomic<int> g_reachEnginePauseCache{-1};
} // namespace reach_bridge
namespace
{
    constexpr size_t kReachCineSigLength = 111;
    constexpr const char* kReachCineVerifyName = "cinematic globals non deterministic";

    struct ReachCineProbeState
    {
        std::atomic<bool> armed{false};
        std::atomic<uint32_t> attemptedGeneration{0};
        uint32_t* tlsIndex = nullptr; // inside the retained Reach module
        uint32_t slotA = 0;
        uint32_t slotB = 0; // 0 = member B unavailable
        // Single-writer seqlock: the engine render thread publishes, the 50 ms
        // worker consumes. Even seq = stable.
        std::atomic<uint32_t> seq{0};
        uint32_t bufA[kReachCineMemberADwords] = {};
        uint32_t bufB[kReachCineMemberBDwords] = {};
        std::atomic<uint32_t> sampleFailures{0};
        // Bumped on every successful (re)arm so the worker-side log state
        // rebaselines instead of diffing against a previous generation.
        std::atomic<uint32_t> logReset{0};
        // Cut-detection history, owned by the render thread inside
        // ReachBuildHeadCullCamera (single writer, no locking needed).
        uint32_t cutPrevStamp = 0;
        bool cutPrevVisible = false;
        bool cutPrevValid = false;
        // Previous frame's authored camera pose, kept only while a cinematic is
        // running, for the shot-cut discontinuity test below.
        float cutPrevPos[3] = {};
        float cutPrevFwd[3] = {};
        bool cutPrevPoseValid = false;
    };
    constinit ReachCineProbeState g_reachCineProbe;

    // Engine render thread, once per owned outer pass. Deterministic and
    // allocation/log-free per the hot-path rules; the SEH guard mirrors the
    // accepted Halo 3 ReadCinematicControl TLS read.
    void ReachCineProbeSample()
    {
        if (!g_reachCineProbe.armed.load(std::memory_order_acquire))
            return;
        uint32_t* tlsIndexPtr = g_reachCineProbe.tlsIndex;
        if (!tlsIndexPtr)
            return;
        __try
        {
            auto** slots = reinterpret_cast<void**>(__readgsqword(0x58));
            if (!slots)
            {
                g_reachCineProbe.sampleFailures.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            const uint32_t tlsIndex = *tlsIndexPtr;
            if (tlsIndex >= 0x200)
            {
                g_reachCineProbe.sampleFailures.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            auto* block = reinterpret_cast<unsigned char*>(slots[tlsIndex]);
            if (!block)
            {
                g_reachCineProbe.sampleFailures.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            auto* memberA = *reinterpret_cast<unsigned char**>(block + g_reachCineProbe.slotA);
            if (!memberA)
            {
                g_reachCineProbe.sampleFailures.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            unsigned char* memberB = nullptr;
            if (g_reachCineProbe.slotB)
            {
                memberB = *reinterpret_cast<unsigned char**>(block + g_reachCineProbe.slotB);
            }
            const uint32_t seqBefore = g_reachCineProbe.seq.load(std::memory_order_relaxed);
            g_reachCineProbe.seq.store(seqBefore + 1, std::memory_order_release);
            memcpy(g_reachCineProbe.bufA, memberA, sizeof(g_reachCineProbe.bufA));
            if (memberB)
            {
                memcpy(g_reachCineProbe.bufB, memberB, sizeof(g_reachCineProbe.bufB));
            }
            g_reachCineProbe.seq.store(seqBefore + 2, std::memory_order_release);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("sampling the Reach cinematic probe faulted");
            g_reachCineProbe.sampleFailures.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // Adds one eye's exact OpenXR separation/cant and stamps the already
    // validated symmetric raster FOV. Missing or non-finite input fails the
    // whole transaction; there is no fixed-IPD or stale-view fallback.
    bool ReachApplyEyeOffset(unsigned char* cam, const ReachEyeRenderInput& input, bool preserveAuthoredFov)
    {
        if (!cam || !input.rasterCover.valid)
            return false;

        float eyeOrientation[4] = {input.frustum.relativeOrientation[0], input.frustum.relativeOrientation[1],
            input.frustum.relativeOrientation[2], input.frustum.relativeOrientation[3]};
        float orientationLengthSquared = 0.0f;
        for (float component : eyeOrientation)
        {
            if (!isfinite(component))
                return false;
            orientationLengthSquared += component * component;
        }
        if (!isfinite(orientationLengthSquared) || orientationLengthSquared <= 1e-8f)
        {
            return false;
        }
        const float orientationLength = sqrtf(orientationLengthSquared);
        for (float& component : eyeOrientation)
            component /= orientationLength;

        float* pos = reinterpret_cast<float*>(cam + 0x00);
        float* fwd = reinterpret_cast<float*>(cam + 0x0C);
        float* up = reinterpret_cast<float*>(cam + 0x18);
        const float right[3] = {
            fwd[1] * up[2] - fwd[2] * up[1], fwd[2] * up[0] - fwd[0] * up[2], fwd[0] * up[1] - fwd[1] * up[0]};
        const float s = kReachWorldUnitsPerMeter;
        for (int axis = 0; axis < 3; ++axis)
            pos[axis] +=
                (right[axis] * input.position[0] + up[axis] * input.position[1] - fwd[axis] * input.position[2]) * s;
        const float sinHalf = sqrtf(eyeOrientation[0] * eyeOrientation[0] + eyeOrientation[1] * eyeOrientation[1] +
                                    eyeOrientation[2] * eyeOrientation[2]);
        if (sinHalf > 1e-5f)
        {
            float angle = 2.0f * atan2f(sinHalf, eyeOrientation[3]);
            if (angle > 3.14159265f)
                angle -= 6.2831853f;
            const float ax = eyeOrientation[0] / sinHalf;
            const float ay = eyeOrientation[1] / sinHalf;
            const float az = eyeOrientation[2] / sinHalf;
            const float axisVec[3] = {ax * right[0] + ay * up[0] - az * fwd[0],
                ax * right[1] + ay * up[1] - az * fwd[1], ax * right[2] + ay * up[2] - az * fwd[2]};
            const float cosA = cosf(angle), sinA = sinf(angle);
            RotateAboutAxis(fwd, axisVec, cosA, sinA);
            RotateAboutAxis(up, axisVec, cosA, sinA);
        }
        if (!preserveAuthoredFov)
        {
            *reinterpret_cast<float*>(cam + 0x28) = input.rasterCover.verticalFov;
        }
        return true;
    }

    // Build the single head-centre camera consumed by Reach's outer CPU
    // visibility pass. The frustum/projection helpers are the exact stock
    // pre-scope pair. The caller commits this same centre to the bounded
    // player-view state before the inner owner is acquired. The claimed inner
    // transaction replaces it with each exact eye and rolls it back afterward;
    // a claimed failure is suppressed rather than rerendered.
    bool ReachBuildHeadCullCamera(const unsigned char* stockCompact, const ReachSymmetricFovCover& cullCover,
        const ReachVrRenderSnapshot& tracking, const ReachVehicleFrame* vehicle,
        const ReachSeatLeaseKey* entryRecenterKey, ReachPendingSeatRecenter& pendingSeatRecenter,
        unsigned char* headCenter, unsigned char* headDerived, float authoredAspect, bool& authoredTheater,
        bool& vehicleViewApplied)
    {
        authoredTheater = false;
        vehicleViewApplied = false;
        pendingSeatRecenter = {};
        // Log-only cinematic-state sample on the engine thread that owns the
        // per-frame camera work. If cutscenes stop this path from running, the
        // worker-side stall report is itself the finding.
        ReachCineProbeSample();
        if (!stockCompact || !headCenter || !headDerived || !cullCover.valid || !g_reachHelpers.Ready())
        {
            return false;
        }
        // Publish the game's own facing yaw from the pristine stock camera
        // forward (offset 0x0C, same convention as the recenter reference at
        // g_gameYawRef = atan2f(fwd[1], fwd[0])) BEFORE the head transform
        // overwrites it. Head-relative locomotion reads this to rotate the move
        // stick to gaze. Read-only: this does not modify stockCompact.
        {
            const float* stockFwd = reinterpret_cast<const float*>(stockCompact + 0x0C);
            const float sfx = stockFwd[0], sfy = stockFwd[1];
            if (isfinite(sfx) && isfinite(sfy) && (sfx * sfx + sfy * sfy) > 1e-8f)
            {
                g_reachStockHeadingYaw.store(atan2f(sfy, sfx), std::memory_order_relaxed);
                g_reachMoveHeadingValid.store(true, std::memory_order_release);
                if (isfinite(stockFwd[2]))
                {
                    // Preserve the legacy compact-camera publication as the
                    // baseline fallback. Seated unit aim travels only
                    // through the coherent publication below, so a stale/torn
                    // coherent read can never fall back to a stale or
                    // component-torn unit vector.
                    g_aimFwdX.store(stockFwd[0]);
                    g_aimFwdY.store(stockFwd[1]);
                    g_aimFwdZ.store(stockFwd[2]);
                    g_aimSeen.store(true, std::memory_order_release);

                    // Seated, publish normalized unit +0x214 aim: the exact
                    // state HREK weapons.cpp consumes for projectile forward.
                    // View-follow does not select this source.
                    const bool seated = vehicle && vehicle->active;
                    const ReachAimFeedbackSource aimSource = ReachSelectAimFeedbackSource(
                        seated, seated && vehicle->unitAimValid, g_config.vehicle_view_follow);
                    const float* aimForward =
                        aimSource == ReachAimFeedbackSource::SeatedUnitAim ? vehicle->unitAimForward : stockFwd;
                    ReachPublishAimFeedback(
                        g_reachCamera.generation.load(std::memory_order_relaxed), aimSource, aimForward);
                }
            }
        }
        // Authored camera-cut detection (probe-proven, headset log
        // 2026-07-27 00:43-00:48): cinematic-globals +0x28 is an authored shot
        // stamp that holds still through ordinary gameplay, and the byte at
        // +0x26 rises exactly when a fade-to-black ends. Either edge is an
        // authored discontinuity, so request the yaw realign ReachApplyHeadLook
        // performs this same frame. The sampler ran at function entry on this
        // thread, so bufA is this frame's coherent copy. Fail-open: an unarmed
        // probe means no realign - stock behavior - never a wrong one.
        //
        // +0x28 alone is NOT sufficient. It was accepted as an every-cut marker
        // on the strength of a session that only watched cutscene starts and
        // ends; the 2026-07-27 08:12-08:14 log then showed it moving just twice
        // across a 65-second cutscene, and headset testing confirmed that
        // the first shot orients but every later shot does not. The camera
        // discontinuity test below is what covers the shots it misses.
        CinematicControlState reachCinematicControl = CinematicControlState::Unknown;
        if (g_reachCineProbe.armed.load(std::memory_order_acquire))
        {
            const uint32_t stateWord = g_reachCineProbe.bufA[0x24 / 4];
            const uint32_t shotStamp = g_reachCineProbe.bufA[0x28 / 4];
            const bool screenVisible = ((stateWord >> 16) & 0xFFu) != 0;
            // Byte +0x24 is the cinematic-in-progress flag, from the same
            // headset log that proved +0x26 and +0x28: it reads 0 in the menu
            // and during ordinary play, rises to 1 for the whole cutscene, and
            // drops back to 0 on the frame the cutscene hands back to gameplay.
            const bool cinematicRunning = (stateWord & 0xFFu) != 0;
            reachCinematicControl = ClassifyReachCinematicControl(true, stateWord);
            bool cut = g_reachCineProbe.cutPrevValid && screenVisible &&
                       (shotStamp != g_reachCineProbe.cutPrevStamp || !g_reachCineProbe.cutPrevVisible);

            // The stamp above only moves at some authored boundaries - a
            // headset log (2026-07-27 08:12-08:14) showed it changing twice
            // across a 65-second cutscene that plainly cut far more often, so
            // every shot in between kept the previous shot's facing. Detect the
            // cut from the authored camera instead: within a shot the cinematic
            // camera moves continuously, and at a cut it jumps. stockCompact is
            // the pristine engine camera for this frame (the head transform
            // below writes headCenter, never this), so during a cinematic it is
            // the authored pose and nothing else. Gated on cinematicRunning so
            // this can never fire during play, where the same camera legitimately
            // jumps on snap turn, respawn, and vehicle entry.
            const float* stockPos = reinterpret_cast<const float*>(stockCompact + 0x00);
            const float* stockFwd = reinterpret_cast<const float*>(stockCompact + 0x0C);
            bool poseUsable = cinematicRunning && screenVisible;
            for (int i = 0; poseUsable && i < 3; ++i)
            {
                if (!isfinite(stockPos[i]) || !isfinite(stockFwd[i]))
                    poseUsable = false;
            }
            if (poseUsable && (stockFwd[0] * stockFwd[0] + stockFwd[1] * stockFwd[1]) <= 1e-8f)
            {
                poseUsable = false;
            }
            if (poseUsable && g_reachCineProbe.cutPrevPoseValid && !cut)
            {
                const float yawDelta =
                    fabsf(WrapPi(atan2f(stockFwd[1], stockFwd[0]) -
                                 atan2f(g_reachCineProbe.cutPrevFwd[1], g_reachCineProbe.cutPrevFwd[0])));
                const float dx = stockPos[0] - g_reachCineProbe.cutPrevPos[0];
                const float dy = stockPos[1] - g_reachCineProbe.cutPrevPos[1];
                const float dz = stockPos[2] - g_reachCineProbe.cutPrevPos[2];
                if (yawDelta > kReachCineCutYawRadians || (dx * dx + dy * dy + dz * dz) > kReachCineCutJumpUnitsSq)
                {
                    cut = true;
                    g_reachCineCutPoseCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
            if (cut)
            {
                g_reachCineCutRealign.store(true, std::memory_order_release);
                g_reachCineCutCount.fetch_add(1, std::memory_order_relaxed);
            }

            if (poseUsable)
            {
                memcpy(g_reachCineProbe.cutPrevPos, stockPos, sizeof(g_reachCineProbe.cutPrevPos));
                memcpy(g_reachCineProbe.cutPrevFwd, stockFwd, sizeof(g_reachCineProbe.cutPrevFwd));
            }
            g_reachCineProbe.cutPrevPoseValid = poseUsable;
            g_reachCineProbe.cutPrevStamp = shotStamp;
            g_reachCineProbe.cutPrevVisible = screenVisible;
            g_reachCineProbe.cutPrevValid = true;
        }
        const uint32_t cinematicGeneration = g_reachCamera.generation.load(std::memory_order_relaxed);
        if (cinematicGeneration)
        {
            TitleAdapter_PublishCinematicControl(
                GameTitle::HaloReach, cinematicGeneration, reachCinematicControl, GetTickCount64());
        }
        memcpy(headCenter, stockCompact, kReachCompactCameraBytes);
        g_reachStockVerticalFov.store(*reinterpret_cast<const float*>(stockCompact + 0x28), std::memory_order_relaxed);
        // The firing hook runs on the engine's weapon path and must not read
        // cinematic state from that thread; publish it from here instead.
        g_reachCinematicLocked.store(
            reachCinematicControl == CinematicControlState::AuthoredLocked ? 1u : 0u, std::memory_order_relaxed);
        authoredTheater =
            reachCinematicControl == CinematicControlState::AuthoredLocked && VR_IsCutsceneTheaterActive();
        if (vehicle && vehicle->active && reachCinematicControl != CinematicControlState::AuthoredLocked)
        {
            memcpy(headCenter + 0x00, vehicle->cameraBase, sizeof(vehicle->cameraBase));
            vehicleViewApplied = true;
        }
        if (!authoredTheater)
        {
            ApplyVrTurn(tracking.pad);
            if (!ReachApplyHeadLook(headCenter, tracking, vehicleViewApplied ? vehicle : nullptr,
                    vehicleViewApplied ? entryRecenterKey : nullptr, pendingSeatRecenter))
                return false;
        }
        if (!authoredTheater)
            *reinterpret_cast<float*>(headCenter + 0x28) = cullCover.verticalFov;

        ReachCompactCameraObservation transformed{};
        if (!ValidateReachCompactCamera(headCenter, kReachCompactCameraBytes, transformed))
        {
            return false;
        }
        float frustumBounds[4]{};
        if (!g_reachHelpers.frustum(headCenter, frustumBounds))
            return false;
        for (float component : frustumBounds)
            if (!isfinite(component))
                return false;
        g_reachHelpers.projection(headCenter, frustumBounds, headDerived, 0.0f);
        const float* projectionMatrix = reinterpret_cast<const float*>(headDerived + kReachDerivedProjectionOffset);
        const ReachProjectionHalfFovs rasterFov =
            DecodeReachProjectionHalfFovs(projectionMatrix[0], projectionMatrix[5]);
        return authoredTheater ? rasterFov.valid && CutsceneTheaterProjectionMatchesAspect(
                                                        authoredAspect, projectionMatrix[0], projectionMatrix[5])
                               : ReachProjectionCoversOpenXr(rasterFov, cullCover);
    }

    // Byte-exact rollback of everything the per-eye transaction can touch,
    // preserving the final eye's actual post-render last-window flag. Runs in a
    // __finally so it executes even if a stock render faults mid-transaction.
    void ReachRestoreScope(
        uintptr_t workspace, uintptr_t playerView, const unsigned char* savedWorkspace, const unsigned char* savedPv)
    {
        const uint8_t finalByte = *reinterpret_cast<uint8_t*>(playerView + kReachLastWindowFlagOffset);
        memcpy(reinterpret_cast<void*>(workspace), savedWorkspace, kReachRenderScopeSnapshotSize);
        memcpy(reinterpret_cast<void*>(playerView + kReachPvSnapshotBegin), savedPv, kReachPvSnapshotBytes);
        *reinterpret_cast<uint8_t*>(playerView + kReachLastWindowFlagOffset) = finalByte;
    }

    // Returns true only if the already-claimed transaction rendered and
    // captured both eyes. False is terminal for this title generation: the
    // caller must revoke ownership and must not invoke the flat renderer.
    // Reach's own script function chud_show_crosshair (haloreach.dll+0x1B3190)
    // hides the crosshair by writing 0.0f into a per-widget alpha field:
    //
    //   chud_globals = *(void**)(TLS[*(uint32_t*)(base+0xC17B18)] + 0x5B0)
    //   alpha_n      = *(float*)(chud_globals + 0x334 + n*0xC60), n = 0..15
    //
    // chud_fade_crosshair_for_player (+0x1B3528) writes the same +0x334 field
    // (and +0x358/+0x37C for a timed fade), which confirms the meaning. The
    // float it stores for "shown" is 1.0f at haloreach.dll+0xA8AF18.
    //
    // Reproducing that exact native write is how Reach honours kill_reticle.
    // It hooks nothing, allocates nothing, and cannot affect camera ownership:
    // every step is guarded, and failure simply leaves the stock crosshair up.
    constexpr size_t kReachChudGlobalsTlsOffset = 0x5B0;
    // The CHUD widget alpha state is three parallel arrays of nine widgets.
    // Derived by disassembling every chud_fade_*_for_player implementation in
    // haloreach.dll and reading the displacement each one writes:
    //
    //   current alpha  +0x32C + i*4      fade target +0x350 + i*4
    //   fade duration  +0x374 + i*4
    //
    //   i=1 weapon stats   i=2 CROSSHAIR    i=3 shield     i=4 grenades
    //   i=5 messages       i=6 motion sensor i=7 chapter title i=8 cinematics
    //
    // Crosshair therefore owns exactly these three fields, and nothing else.
    // Setting only the current alpha is not enough: the engine drives current
    // toward the fade target every frame, so the target must be cleared too.
    constexpr size_t kReachChudCrosshairAlpha = 0x334;
    constexpr size_t kReachChudCrosshairFadeTarget = 0x358;
    constexpr size_t kReachChudCrosshairFadeDuration = 0x37C;

    // Resolved once on the cold title worker, never guessed. Chain:
    //   "chud_show_crosshair\0"   -> exactly one occurrence in the image
    //   exactly one qword -> it   -> that script-function table entry
    //   entry + 0x18              -> the implementation
    //   implementation + 0x30     -> its own 'mov ecx,[rip+rel32]' TLS index
    // Every step is required to be unique/exact; anything else leaves the stock
    // crosshair alone.
    constinit const uint32_t* g_reachChudTlsIndex = nullptr;

    bool ResolveReachChudCrosshairFields(uintptr_t base, size_t size)
    {
        g_reachChudTlsIndex = nullptr;
        static const char kName[] = "chud_show_crosshair";
        const uint8_t* image = reinterpret_cast<const uint8_t*>(base);
        constexpr size_t kNameBytes = sizeof(kName); // includes the NUL

        uintptr_t nameVa = 0;
        size_t nameHits = 0;
        for (size_t i = 0; i + kNameBytes <= size;)
        {
            const void* hit = memchr(image + i, kName[0], size - i - kNameBytes + 1);
            if (!hit)
                break;
            const size_t at = static_cast<size_t>(reinterpret_cast<const uint8_t*>(hit) - image);
            if (memcmp(image + at, kName, kNameBytes) == 0)
            {
                nameVa = base + at;
                if (++nameHits > 1)
                    break;
            }
            i = at + 1;
        }
        if (nameHits != 1)
            return false;

        uintptr_t entry = 0;
        size_t pointerHits = 0;
        for (size_t i = 0; i + sizeof(uintptr_t) <= size; i += sizeof(uintptr_t))
        {
            if (*reinterpret_cast<const uintptr_t*>(image + i) == nameVa)
            {
                entry = base + i;
                if (++pointerHits > 1)
                    break;
            }
        }
        if (pointerHits != 1)
            return false;

        const uintptr_t impl = *reinterpret_cast<const uintptr_t*>(entry + 0x18);
        if (impl < base || impl + 0x40 > base + size)
            return false;

        // The implementation's own prologue and its TLS-index instruction.
        // clang-format off
        static const uint8_t kPrologue[] = {
            0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x0F,
            0xBF, 0xC1, 0x8B, 0xDA, 0x45, 0x8A, 0xC8};
        // clang-format on
        const uint8_t* fn = reinterpret_cast<const uint8_t*>(impl);
        if (memcmp(fn, kPrologue, sizeof(kPrologue)) != 0)
            return false;
        if (fn[0x30] != 0x8B || fn[0x31] != 0x0D)
            return false;
        int32_t displacement = 0;
        memcpy(&displacement, fn + 0x32, sizeof(displacement));
        const uintptr_t tlsIndexVa = impl + 0x36 + static_cast<uintptr_t>(static_cast<intptr_t>(displacement));
        if (tlsIndexVa < base || tlsIndexVa + sizeof(uint32_t) > base + size)
            return false;

        g_reachChudTlsIndex = reinterpret_cast<const uint32_t*>(tlsIndexVa);
        LOG("Reach crosshair: chud_show_crosshair resolved at haloreach.dll+"
            "0x%llX through its unique script-table entry; CHUD TLS index at "
            "+0x%llX",
            static_cast<unsigned long long>(impl - base), static_cast<unsigned long long>(tlsIndexVa - base));
        return true;
    }

    // Consumer qualification for Reach's presented ray. Both halves are
    // required: the ray's own producer receipt (epoch/trust/prepared serial,
    // under the staleness bound) AND a title-native invocation receipt for the
    // consuming frame that still proves the same relationship owner against the
    // LIVE durable relationship. Matching the durable epoch alone is never
    // permission (the A->B counterexample), and blind serial equality is not
    // required: a completed ray may legitimately be one serial old. False means
    // "refuse the VR ray"; the caller keeps its existing native/stock
    // behaviour. PG off / unwired title: the ray carries no support receipt and
    // this returns true, byte-identical to the base path.
    bool ReachPresentedRayConsumable(GameTitle title, uint32_t generation, uint64_t consumerSerial,
        uint64_t raySupportEpoch, bool raySupportTrusted, uint64_t raySerial)
    {
        if (!VR_SupportGripWiredForTitle(title))
            return true;
        support_grip::SupportInvocationReceipt invocation{};
        SupportGripRelationshipSnapshot relationship{};
        if (!VR_ReadSupportInvocationReceipt(title, invocation) || !VR_GetSupportGripRelationship(relationship))
            return false; // fail closed: an unproven invocation is no proof
        return support_grip::PresentedRayConsumableForInvocation(title, invocation, generation, consumerSerial,
            /*relationshipReadable=*/true, relationship.engaged, relationship.epoch,
            support_grip::OwnerTuple{
                relationship.title, relationship.generation, relationship.unit, relationship.weapon},
            raySupportEpoch, raySupportTrusted, raySerial);
    }

    // `outOrigin` is where the presented sight ray STARTS, in the same
    // game space as `outTarget`. Both come from one controller pose through one
    // transform, so a shot fired from the origin toward the target is the exact
    // line the player sees - at every range, not just at crosshair_distance_m.
    bool ReachBuildPresentedReticleWorldTarget(const ReachOwnerScope& owner, uint32_t generation, float outTarget[3],
        float outOrigin[3], uint64_t& outSampleMs, uint64_t* outSupportEpoch = nullptr,
        bool* outSupportTrusted = nullptr, uint64_t* outRaySerial = nullptr)
    {
        if (outSupportEpoch)
            *outSupportEpoch = 0;
        if (outSupportTrusted)
            *outSupportTrusted = false;
        if (outRaySerial)
            *outRaySerial = 0;
        if (!outTarget || !outOrigin || !owner.active)
        {
            return false;
        }

        float q[4]{};
        float p[3]{};
        uint64_t sampleMs = 0;
        uint64_t supportEpoch = 0;
        bool supportTrusted = false;
        uint64_t raySerial = 0;
        if (!VR_GetPresentedReticleAimPoseWithSupportProvenance(
                q, p, sampleMs, supportEpoch, supportTrusted, raySerial))
            return false;
        // The ray must belong to this consuming frame (serial staleness
        // bound) and its producer receipt must still agree with the live
        // relationship AND this title invocation's own owner proof.
        if (!ReachPresentedRayConsumable(
                GameTitle::HaloReach, generation, owner.preparedSerial, supportEpoch, supportTrusted, raySerial))
            return false;
        if (outSupportEpoch)
            *outSupportEpoch = supportEpoch;
        if (outSupportTrusted)
            *outSupportTrusted = supportTrusted;
        if (outRaySerial)
            *outRaySerial = raySerial;
        float qLengthSquared = 0.0f;
        for (float component : q)
        {
            if (!std::isfinite(component))
                return false;
            qLengthSquared += component * component;
        }
        for (float component : p)
            if (!std::isfinite(component))
                return false;
        if (!std::isfinite(qLengthSquared) || qLengthSquared <= 1.0e-8f)
            return false;
        const float inverseQ = 1.0f / sqrtf(qLengthSquared);
        for (float& component : q)
            component *= inverseQ;

        const float localForward[3] = {0.0f, 0.0f, -1.0f};
        float aimForward[3]{};
        RotateByQuat(q, localForward, aimForward);
        const float distance = Clamp(g_config.crosshair_distance_m, 2.0f, 50.0f);
        const float localTarget[3] = {
            p[0] + aimForward[0] * distance, p[1] + aimForward[1] * distance, p[2] + aimForward[2] * distance};

        const float sh = sinf(g_headYawRef);
        const float ch = cosf(g_headYawRef);
        float hullYaw = 0.0f;
        float hullPitch = 0.0f;
        float positionBasis[9]{};
        const bool follows =
            Halo3ReadRollStableFollow(hullYaw, hullPitch) &&
            Halo3ComposeFollowBasisWithRoll(hullYaw, hullPitch, g_gameYawRef, 0.0f, 0.0f, 0.0f, positionBasis);
        // One transform, applied to both ends of the same ray. Any difference
        // between them would reintroduce exactly the parallax this removes.
        const auto toGameSpace = [&](const float local[3], float out[3]) -> bool
        {
            const float dx = local[0] - g_headPosRef[0];
            const float dy = local[1] - g_headPosRef[1];
            const float dz = local[2] - g_headPosRef[2];
            const float roomForward = dx * sh - dz * ch;
            const float roomRight = dx * ch + dz * sh;
            const float positionLocal[3] = {roomForward, -roomRight, dy};
            float offset[3]{};
            if (!follows || !Halo3TransformBasisVector(positionBasis, positionLocal, offset))
            {
                const float cg = cosf(g_gameYawRef);
                const float sg = sinf(g_gameYawRef);
                offset[0] = cg * roomForward + sg * roomRight;
                offset[1] = sg * roomForward - cg * roomRight;
                offset[2] = dy;
            }
            for (int axis = 0; axis < 3; ++axis)
            {
                // The OpenXR quad is positioned in the same local space as the
                // headset projection, so its room origin maps to the authored
                // camera base. gameplayBasePosition may deliberately select the
                // rigid hands base and would offset the sight when
                // body-following hands are enabled.
                const float base = owner.vehicleViewApplied && owner.vehicle.active
                                       ? owner.vehicle.cameraBase[axis]
                                       : owner.presentedCameraBasePosition[axis];
                out[axis] = base + offset[axis] * kReachWorldUnitsPerMeter;
                if (!std::isfinite(out[axis]))
                    return false;
            }
            return true;
        };
        if (!toGameSpace(localTarget, outTarget) || !toGameSpace(p, outOrigin))
            return false;
        outSampleMs = sampleMs;
        return sampleMs != 0;
    }

    void ReachPublishCompletedFrameFeedback(uint32_t generation, uint64_t preparedSerial, const ReachOwnerScope& owner)
    {
        ReachReticleAimSample reticle{};
        reticle.generation = generation;
        reticle.preparedSerial = preparedSerial;
        const bool seated = generation && preparedSerial && owner.vehicleViewApplied && owner.vehicle.active;
        if (seated)
        {
            reticle.occupation = {generation, owner.vehicle.unitHandle, owner.vehicle.directParent,
                owner.vehicle.definitionDatum, owner.vehicle.seatIndex};
        }

        {
            auto& published = g_reachCompletedReticleAim;
            published.sequence.fetch_add(1, std::memory_order_acq_rel);
            published.generation.store(reticle.generation, std::memory_order_relaxed);
            published.preparedSerial.store(reticle.preparedSerial, std::memory_order_relaxed);
            published.source.store(static_cast<uint8_t>(reticle.source), std::memory_order_relaxed);
            published.unitHandle.store(reticle.occupation.unitHandle, std::memory_order_relaxed);
            published.directParent.store(reticle.occupation.directParent, std::memory_order_relaxed);
            published.definitionDatum.store(reticle.occupation.definitionDatum, std::memory_order_relaxed);
            published.seatIndex.store(reticle.occupation.seatIndex, std::memory_order_relaxed);
            published.identity.store(
                seated ? static_cast<uint8_t>(owner.vehicle.identity) : static_cast<uint8_t>(ReachVehicleId::Unknown),
                std::memory_order_relaxed);
            published.x.store(reticle.cameraLocalDirection[0], std::memory_order_relaxed);
            published.y.store(reticle.cameraLocalDirection[1], std::memory_order_relaxed);
            published.z.store(reticle.cameraLocalDirection[2], std::memory_order_relaxed);
            published.sequence.fetch_add(1, std::memory_order_release);
        }

        float presentedTarget[3]{};
        float presentedOrigin[3]{};
        uint64_t presentedTargetMs = 0;
        uint64_t presentedSupportEpoch = 0;
        bool presentedSupportTrusted = false;
        uint64_t presentedRaySerial = 0;
        {
            auto& published = g_reachOnFootShotRay;
            const int32_t unit = LegacyCollisionIgnoredObject(GameTitle::HaloReach);
            const bool onFoot =
                generation && preparedSerial && unit != -1 && !owner.vehicleViewApplied && !owner.vehicle.active &&
                !owner.cutsceneTheater && owner.fpTargets.rightWristValid &&
                ReachBuildPresentedReticleWorldTarget(owner, generation, presentedTarget, presentedOrigin,
                    presentedTargetMs, &presentedSupportEpoch, &presentedSupportTrusted, &presentedRaySerial);
            published.sequence.fetch_add(1, std::memory_order_acq_rel);
            published.generation.store(onFoot ? generation : 0, std::memory_order_relaxed);
            published.sampleMs.store(onFoot ? presentedTargetMs : 0, std::memory_order_relaxed);
            published.unitHandle.store(onFoot ? unit : -1, std::memory_order_relaxed);
            published.x.store(presentedTarget[0], std::memory_order_relaxed);
            published.y.store(presentedTarget[1], std::memory_order_relaxed);
            published.z.store(presentedTarget[2], std::memory_order_relaxed);
            published.originX.store(presentedOrigin[0], std::memory_order_relaxed);
            published.originY.store(presentedOrigin[1], std::memory_order_relaxed);
            published.originZ.store(presentedOrigin[2], std::memory_order_relaxed);
            published.supportEpoch.store(onFoot ? presentedSupportEpoch : 0, std::memory_order_relaxed);
            published.supportTrusted.store(onFoot && presentedSupportTrusted ? 1u : 0u, std::memory_order_relaxed);
            published.raySerial.store(onFoot ? presentedRaySerial : 0, std::memory_order_relaxed);
            published.sequence.fetch_add(1, std::memory_order_release);
        }
        presentedSupportEpoch = 0;
        presentedSupportTrusted = false;
        presentedRaySerial = 0;
        const bool targetValid =
            seated && ReachSeatLeaseKeyValid(reticle.occupation) &&
            ReachBuildPresentedReticleWorldTarget(owner, generation, presentedTarget, presentedOrigin,
                presentedTargetMs, &presentedSupportEpoch, &presentedSupportTrusted, &presentedRaySerial);
        {
            auto& published = g_reachCompletedReticleTarget;
            published.sequence.fetch_add(1, std::memory_order_acq_rel);
            published.generation.store(targetValid ? generation : 0, std::memory_order_relaxed);
            published.sampleMs.store(targetValid ? presentedTargetMs : 0, std::memory_order_relaxed);
            published.unitHandle.store(targetValid ? reticle.occupation.unitHandle : -1, std::memory_order_relaxed);
            published.directParent.store(targetValid ? reticle.occupation.directParent : -1, std::memory_order_relaxed);
            published.definitionDatum.store(
                targetValid ? reticle.occupation.definitionDatum : 0xFFFFFFFFu, std::memory_order_relaxed);
            published.seatIndex.store(targetValid ? reticle.occupation.seatIndex : -1, std::memory_order_relaxed);
            published.x.store(targetValid ? presentedTarget[0] : 0.0f, std::memory_order_relaxed);
            published.y.store(targetValid ? presentedTarget[1] : 0.0f, std::memory_order_relaxed);
            published.z.store(targetValid ? presentedTarget[2] : 0.0f, std::memory_order_relaxed);
            published.originX.store(targetValid ? presentedOrigin[0] : 0.0f, std::memory_order_relaxed);
            published.originY.store(targetValid ? presentedOrigin[1] : 0.0f, std::memory_order_relaxed);
            published.originZ.store(targetValid ? presentedOrigin[2] : 0.0f, std::memory_order_relaxed);
            published.supportEpoch.store(targetValid ? presentedSupportEpoch : 0, std::memory_order_relaxed);
            published.supportTrusted.store(targetValid && presentedSupportTrusted ? 1u : 0u, std::memory_order_relaxed);
            published.raySerial.store(targetValid ? presentedRaySerial : 0, std::memory_order_relaxed);
            published.sequence.fetch_add(1, std::memory_order_release);
        }

        const float* eye = reinterpret_cast<const float*>(owner.headCenter + 0x00);
        const bool eyeValid = seated && ReachSeatLeaseKeyValid(reticle.occupation) && std::isfinite(eye[0]) &&
                              std::isfinite(eye[1]) && std::isfinite(eye[2]);
        const uint64_t sampleMs = eyeValid ? GetTickCount64() : 0;
        {
            auto& published = g_reachCompletedEye;
            published.sequence.fetch_add(1, std::memory_order_acq_rel);
            published.generation.store(eyeValid ? generation : 0, std::memory_order_relaxed);
            published.preparedSerial.store(eyeValid ? preparedSerial : 0, std::memory_order_relaxed);
            published.sampleMs.store(sampleMs, std::memory_order_relaxed);
            published.unitHandle.store(eyeValid ? reticle.occupation.unitHandle : -1, std::memory_order_relaxed);
            published.directParent.store(eyeValid ? reticle.occupation.directParent : -1, std::memory_order_relaxed);
            published.definitionDatum.store(
                eyeValid ? reticle.occupation.definitionDatum : 0xFFFFFFFFu, std::memory_order_relaxed);
            published.seatIndex.store(eyeValid ? reticle.occupation.seatIndex : -1, std::memory_order_relaxed);
            published.x.store(eyeValid ? eye[0] : 0.0f, std::memory_order_relaxed);
            published.y.store(eyeValid ? eye[1] : 0.0f, std::memory_order_relaxed);
            published.z.store(eyeValid ? eye[2] : 0.0f, std::memory_order_relaxed);
            published.sequence.fetch_add(1, std::memory_order_release);
        }
    }

    bool ReachWindCopy(void* destination, const void* source, size_t bytes)
    {
        __try
        {
            memcpy(destination, source, bytes);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("copying Reach wind memory faulted");
            g_reachWindReplayFaults.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
    }

    inline void ReachScopeLogCamera(const float* camera, const float* head)
    {
        VR_NoteScopeCamera(GameTitle::HaloReach, camera, head, kReachWorldUnitsPerMeter);
    }
    // The head vertical FOV before the scope pass widened it (render
    // thread), for mode 1 (eyes get it back) and the cap of mode 2.
    constinit float g_reachHeadVerticalBeforeScope = 0;
    constinit bool g_reachHeadVerticalWidened = false;
    inline void ReachScopeLogFov(float before, float after)
    {
        VR_NoteScopeHeadFov(before, after);
        g_reachHeadVerticalBeforeScope = before;
        g_reachHeadVerticalWidened = std::isfinite(after) && after > before;
    }
    inline float ReachScopeCullVertical(float original, float needed)
    {
        // A live-but-not-raised glass pass does not widen the head camera.
        if (!VR_IsScopeActive() && std::isfinite(original) && original > 0)
            return original;
        return g_config.reach_scope_cull_mode == 2
                   ? CapCullVerticalFov(original, needed, g_config.reach_scope_cull_cap_deg * 0.017453292519943295f)
                   : needed;
    }
    inline const float* ReachScopeOrigin(const float* head, const float* barrel, float out[3])
    {
        return VR_ScopeCameraOrigin(GameTitle::HaloReach, kReachWorldUnitsPerMeter, barrel, out) ? out : head;
    }
#include "reach_scope.inl"
    // Diagnostic: once a second while scoped, and once at scope end, one
    // "Reach zoom pass:" line with (a) the engine's zoom (stock vfov narrowing
    // against the widest stock vfov this generation, plus the aim-query zoom
    // level), (b) the FOV each head eye pass was committed with, decoded from
    // the projection matrix handed to the native matrix builder (symmetric, so
    // L=R and U=D), against the OpenXR cover it had to meet, and the size of the
    // surface each eye pass renders into, and (c) the cull mode in force.
    // narrowed=1 when any eye pass is below its cover by more than 0.1 deg.
    void ReachZoomPassLog(const float halfX[2], const float halfY[2], const float coverX[2], const float coverY[2],
        uint32_t eyes, float headVertical, ID3D11Texture2D* surface)
    {
        constexpr float kDeg = 57.2957795f;
        static bool wasScoped = false;
        static uint64_t lastMs = 0;
        static uint32_t widestGeneration = 0;
        static float widest = 0;
        const float stock = g_reachStockVerticalFov.load(std::memory_order_relaxed);
        const uint32_t generation = g_reachCamera.generation.load(std::memory_order_relaxed);
        if (generation != widestGeneration)
        {
            widestGeneration = generation;
            widest = 0;
        }
        if (std::isfinite(stock) && stock > widest && stock < 3.0f)
            widest = stock;
        const bool scoped = VR_IsScopeActive();
        const uint64_t now = GetTickCount64();
        if (!scoped && !wasScoped)
            return;
        if (scoped && wasScoped && now - lastMs < 1000)
            return;
        const bool end = !scoped;
        wasScoped = scoped;
        lastMs = now;
        const float zoom = std::isfinite(stock) && stock > 0 && widest > 0 ? tanf(widest * .5f) / tanf(stock * .5f) : 0;
        const int32_t level = g_reachEngineZoomLevel.load(std::memory_order_relaxed);
        const uint64_t levelMs = g_reachEngineZoomMs.load(std::memory_order_relaxed);
        char levelText[48];
        if (level == -2)
            std::snprintf(levelText, sizeof levelText, "none");
        else
            std::snprintf(levelText, sizeof levelText, "%d (%llu ms ago)", level,
                static_cast<unsigned long long>(levelMs && now >= levelMs ? now - levelMs : 0));
        uint32_t width = 0, height = 0;
        if (surface)
        {
            D3D11_TEXTURE2D_DESC desc{};
            surface->GetDesc(&desc);
            width = desc.Width;
            height = desc.Height;
        }
        char eyeText[2][112];
        bool narrowed = false;
        for (int e = 0; e < 2; ++e)
        {
            if (!(eyes & (1u << e)))
            {
                std::snprintf(eyeText[e], sizeof eyeText[e], "not rendered");
                continue;
            }
            narrowed |= (coverX[e] - halfX[e]) * kDeg > .1f || (coverY[e] - halfY[e]) * kDeg > .1f;
            std::snprintf(eyeText[e], sizeof eyeText[e], "L/R/U/D=%.2f/%.2f/%.2f/%.2f cover %.2f/%.2f rt=%ux%u",
                halfX[e] * kDeg, halfX[e] * kDeg, halfY[e] * kDeg, halfY[e] * kDeg, coverX[e] * kDeg, coverY[e] * kDeg,
                width, height);
        }
        const int mode = g_config.reach_scope_cull_mode;
        char capText[24]{};
        if (mode == 2)
            std::snprintf(capText, sizeof capText, " %.0f deg", g_config.reach_scope_cull_cap_deg);
        LOG("Reach zoom pass: scoped=%d%s game-zoom=x%.2f (engine vfov %.2f deg, widest %.2f) zoom-level=%s | "
            "eyeL %s | eyeR %s | eye FOV from projection matrix | head-vfov-cmd=%.2f cull-mode=%d (%s%s) widened=%d | narrowed=%d",
            scoped ? 1 : 0, end ? " (end)" : "", zoom, stock * kDeg, widest * kDeg, levelText, eyeText[0], eyeText[1],
            headVertical * kDeg, mode,
            mode == 1   ? "eyes get real head FOV"
            : mode == 2 ? "widening capped at"
                        : "widened",
            mode == 2 ? capText : "", g_reachHeadVerticalWidened && g_reachOwnerScope.scope.ready ? 1 : 0,
            narrowed ? 1 : 0);
    }

    // Diagnostic, read only: once a second, the instance occlusion cache of
    // window 0 as the eyes meet it (sampled after readback and visibility, before
    // the frame's first player_view_render submits), split into frames with and
    // without a scope-glass gun. passes/frame = player_view_render calls we made,
    // each of which runs the window-0 per-frame work (0x260830 -> 0x260554).
    constinit reach_occlusion::Second g_reachOcclusionSecond{};
    constinit reach_occlusion::Tally g_reachOcclusionFrame{};
    constinit bool g_reachOcclusionSampled = false;
    void ReachOcclusionSample()
    {
        g_reachOcclusionSampled = false;
        if (!g_reachCamera.base)
            return;
        reach_occlusion::Tally tally{};
        __try
        {
            reach_occlusion::Count(reinterpret_cast<const uint32_t*>(g_reachCamera.base + reach_occlusion::kKeysRva),
                reinterpret_cast<const uint8_t*>(g_reachCamera.base + reach_occlusion::kStateRva),
                reach_occlusion::kEntries, 0, tally);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("sampling Reach occlusion faulted");
            return;
        }
        g_reachOcclusionFrame = tally;
        g_reachOcclusionSampled = true;
    }
    void ReachOcclusionFinish(bool scopeRan, uint32_t passes)
    {
        if (!g_reachOcclusionSampled)
            return;
        g_reachOcclusionSampled = false;
        g_reachOcclusionSecond.Add(g_reachOcclusionFrame, VR_ScopeGlassLive(), scopeRan, passes);
        static uint64_t lastMs = 0;
        const uint64_t now = GetTickCount64();
        if (!lastMs)
            lastMs = now;
        if (now - lastMs < 1000)
            return;
        lastMs = now;
        char line[640];
        reach_occlusion::Format(g_reachOcclusionSecond, line, sizeof line);
        LOG("%s", line);
        g_reachOcclusionSecond = reach_occlusion::Second{};
    }
    // The scope pass reached player_view_render this frame (stage 98/99 is stored only then).
    bool ReachScopePassRan()
    {
        const unsigned stage = g_reachScopeStage.load();
        return g_reachOwnerScope.scope.ready && (stage == 98 || stage == 99);
    }

    bool ReachStereoTransaction(uintptr_t playerView, ReachVrRenderAccess& access)
    {
        const uintptr_t workspace = g_reachOwnerScope.workspace;
        unsigned char* compact = reinterpret_cast<unsigned char*>(workspace);

        if (!g_reachHelpers.Ready() || !access.active || !g_reachOwnerScope.active || !g_reachFpPairScope.armed ||
            g_reachFpPairScope.generation != g_reachCamera.generation ||
            g_reachFpPairScope.preparedSerial != access.preparedSerial ||
            access.preparedSerial != g_reachOwnerScope.preparedSerial)
            return false;

        ReachCompactCameraObservation observed{};
        ReachCompactCameraObservation secondaryObserved{};
        const unsigned char* secondaryCompact =
            reinterpret_cast<const unsigned char*>(workspace + kReachSecondaryCompactOffset);
        if (!ValidateReachCompactCamera(compact, kReachCompactCameraBytes, observed) ||
            !ValidateReachCompactCamera(secondaryCompact, kReachCompactCameraBytes, secondaryObserved) ||
            memcmp(compact, g_reachOwnerScope.headCenter, kReachCompactCameraBytes) != 0 ||
            memcmp(secondaryCompact, g_reachOwnerScope.headCenter, kReachCompactCameraBytes) != 0)
        {
            return false;
        }

        // player_view_render loads the render-camera owner global (0x04E38A90)
        // into rax and dereferences [rax+0xB0], and it CLEARS that global to zero
        // on every call (RVA 0x26CE2F). Left alone, the second eye's call loads
        // null and faults in the first-person camera build -- the observed access
        // violation at haloreach.dll RVA 0x26E02F. Require the engine's admitted
        // owner (player_view + 0x3B0), re-arm the global to it before each eye's
        // call, and never restore it afterward so the final eye's stock zero
        // persists. An unexpected owner rejects the claimed transaction.
        const uintptr_t reachOwnerGlobal = g_reachCamera.base + kReachRenderCameraOwnerRva;
        const uintptr_t entryOwner = *reinterpret_cast<uintptr_t*>(reachOwnerGlobal);
        if (entryOwner != playerView + kReachPlayerViewCameraStateOffset)
            return false;

        // A prepared serial can encounter more than one qualifying outer call.
        // Invalidate the whole eye and authored-CHUD pair before each admitted
        // attempt so a partial retry cannot combine one newly rendered eye, or
        // a no-widget state, with the prior attempt from the same frame serial.
        g_reachRenderFovSerial[0].store(0, std::memory_order_release);
        g_reachRenderFovSerial[1].store(0, std::memory_order_release);
        VR_InvalidatePreparedAuthoredReticleCapture();

        alignas(16) unsigned char savedWorkspace[kReachRenderScopeSnapshotSize];
        alignas(16) unsigned char savedPv[kReachPvSnapshotBytes];
        alignas(16) unsigned char center[kReachCompactCameraBytes];
        memcpy(savedWorkspace, reinterpret_cast<void*>(workspace), kReachRenderScopeSnapshotSize);
        memcpy(savedPv, reinterpret_cast<void*>(playerView + kReachPvSnapshotBegin), kReachPvSnapshotBytes);

        // The outer hook already consumed turn/head pose once before CPU
        // visibility. Both exact eyes must derive from that identical centre;
        // applying either transform again here would double room-scale lean and
        // split culling from the raster camera.
        memcpy(center, g_reachOwnerScope.headCenter, kReachCompactCameraBytes);
        // Cull mode 1: culling already used the widened head camera; the eye
        // passes get the real head FOV back (the suspected LOD/quality input).
        if (g_config.reach_scope_cull_mode == 1 && g_reachHeadVerticalWidened && g_reachOwnerScope.scope.ready &&
            std::isfinite(g_reachHeadVerticalBeforeScope))
            *reinterpret_cast<float*>(center + 0x28) = g_reachHeadVerticalBeforeScope;
        {
            // Proof log: when the scope pass widened the head camera, say whether the
            // eye passes got the widened FOV (mode 0/2) or the real one (mode 1).
            // On each change, and at most once a second while it lasts.
            static int lastState = -1;
            static uint64_t lastLogMs = 0;
            const bool widened = g_reachHeadVerticalWidened && g_reachOwnerScope.scope.ready &&
                                 std::isfinite(g_reachHeadVerticalBeforeScope);
            const float widenedVertical =
                widened ? *reinterpret_cast<const float*>(g_reachOwnerScope.headCenter + 0x28) : 0.0f;
            const int state = !widened ? 0 : g_config.reach_scope_cull_mode == 1 ? 1 : 2;
            const uint64_t logNow = GetTickCount64();
            // Proof log: the angle between where the head camera looks and where the scope
            // camera looks (compact cameras, forward at +0x0C), logged with each line and
            // once a second while the scope pass is prepared, widened or not.
            float headVsScope = NAN;
            if (g_reachOwnerScope.scope.ready)
            {
                const float* hf = reinterpret_cast<const float*>(g_reachOwnerScope.headCenter + 0x0C);
                const float* sf = reinterpret_cast<const float*>(g_reachOwnerScope.scope.compact + 0x0C);
                const float lh = std::sqrt(hf[0] * hf[0] + hf[1] * hf[1] + hf[2] * hf[2]),
                            ls = std::sqrt(sf[0] * sf[0] + sf[1] * sf[1] + sf[2] * sf[2]);
                if (lh > 1e-5f && ls > 1e-5f)
                    headVsScope = std::acos(std::clamp(
                                      (hf[0] * sf[0] + hf[1] * sf[1] + hf[2] * sf[2]) / (lh * ls), -1.0f, 1.0f)) *
                                  57.2957795f;
            }
            if (state != lastState || ((state != 0 || g_reachOwnerScope.scope.ready) && logNow - lastLogMs >= 1000))
            {
                if (state == 0 && lastState > 0)
                    LOG("Reach scope cull: head FOV no longer widened (eyes and culling use the real head FOV); head-vs-scope %.1f deg",
                        headVsScope);
                else if (state != 0)
                    LOG("Reach scope cull: head vfov widened %.1f->%.1f deg for culling; eye passes use the %s FOV (cull mode %d); head-vs-scope %.1f deg",
                        g_reachHeadVerticalBeforeScope * 57.2957795f, widenedVertical * 57.2957795f,
                        state == 1                            ? "real head"
                        : g_config.reach_scope_cull_mode == 2 ? "CAPPED (still widened)"
                                                              : "WIDENED",
                        g_config.reach_scope_cull_mode, headVsScope);
                else
                    LOG("Reach scope cull: not widened; head-vs-scope %.1f deg", headVsScope);
                lastState = state;
                lastLogMs = logNow;
            }
        }

        const uint8_t originalLastWindow = *reinterpret_cast<uint8_t*>(playerView + kReachLastWindowFlagOffset);
        const bool rightFirst = g_config.right_eye_first;
        bool completed = false;
        bool transactionValid = true;
        uint32_t capturedEyes = 0;
        float zoomLogHalfX[2]{}, zoomLogHalfY[2]{}, zoomLogCoverX[2]{}, zoomLogCoverY[2]{};
        uint32_t zoomLogEyes = 0;
        uint32_t committedOuterCameraEyes = 0;
        // reach_scope_order: 0 = scope pass here, before the eyes; 1 (default
        // since v22) = after both eyes (below), so the first player-view render
        // of the frame is the head's and its camera submits the instance
        // occlusion queries the next frame culls by (0x293400 / 0x293AB4).
        const ReachScopeOrder scopeOrder = ReachScopeOrderFrom(g_config.reach_scope_order);
        {
            static int loggedOrder = -1;
            if (loggedOrder != static_cast<int>(scopeOrder))
            {
                loggedOrder = static_cast<int>(scopeOrder);
                LOG("Reach scope pass order: %s", ReachScopeOrderName(scopeOrder));
            }
        }
        ReachOcclusionSample();
        bool scopeRan = false;
        if (scopeOrder == ReachScopeOrder::First)
        {
            ReachRenderScope(playerView, access);
            scopeRan = ReachScopePassRan();
        }
        ReachWindReplay windReplay{};
        void* const windState =
            g_reachWindReplayFaults.load(std::memory_order_relaxed) == 0 ? g_reachCamera.decoratorWindState : nullptr;
        __try
        {
            for (uint32_t pass = 0; pass < 2; ++pass)
            {
                const ReachStereoPassPolicy policy = SelectReachStereoPassPolicy(pass, rightFirst, originalLastWindow);
                if (!policy.valid)
                {
                    transactionValid = false;
                    break;
                }
                g_stereoEye.store(static_cast<int>(policy.eye), std::memory_order_release);
                // Head-tracked centre plus this eye's exact OpenXR separation,
                // cant, and symmetric covering FOV, stamped into the primary
                // compact camera.
                memcpy(compact, center, kReachCompactCameraBytes);
                const ReachEyeRenderInput& eyeInput = g_reachOwnerScope.eyes[policy.eye];
                const ReachSymmetricFovCover& requestedFov = eyeInput.rasterCover;
                if (!ReachApplyEyeOffset(compact, eyeInput, g_reachOwnerScope.cutsceneTheater))
                {
                    transactionValid = false;
                    break;
                }

                // Rebuild this eye exactly as stock setup does, but from the VR
                // camera, so player_view_render renders from per-eye matrices
                // instead of the pre-built stock centre matrices. Without this
                // rebuild the compact-camera edits are never read -- that is the
                // cone and the missing 6DOF the headset showed. This is the
                // proven pre-scope rebuild: frustum bounds -> projection -> primary/secondary
                // coherence -> camera-state update -> projection/matrix build
                // into player_view+0x490. It mirrors Halo 3's RenderViewHook,
                // which rebuilds view+0x98 with the engine's own helpers.
                unsigned char* primaryDerived =
                    reinterpret_cast<unsigned char*>(workspace + kReachPrimaryDerivedOffset);
                float frustumBounds[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                if (!g_reachHelpers.frustum(compact, frustumBounds))
                {
                    transactionValid = false;
                    break;
                }
                bool frustumFinite = true;
                for (float component : frustumBounds)
                    frustumFinite = isfinite(component) && frustumFinite;
                if (!frustumFinite)
                {
                    transactionValid = false;
                    break;
                }
                g_reachHelpers.projection(compact, frustumBounds, primaryDerived, 0.0f);
                const float* projectionMatrix =
                    reinterpret_cast<const float*>(primaryDerived + kReachDerivedProjectionOffset);
                const ReachProjectionHalfFovs rasterFov =
                    DecodeReachProjectionHalfFovs(projectionMatrix[0], projectionMatrix[5]);
                if (!(g_reachOwnerScope.cutsceneTheater
                            ? rasterFov.valid &&
                                  CutsceneTheaterProjectionMatchesAspect(
                                      g_reachOwnerScope.cutsceneTheaterAspect, projectionMatrix[0], projectionMatrix[5])
                            : ReachProjectionCoversOpenXr(rasterFov, requestedFov)))
                {
                    transactionValid = false;
                    break;
                }
                // Optional DLSS: open this eye's depth window (Reach renders
                // its eyes outside the raster-eye scope), add the sub-pixel
                // jitter to the projection centre terms BEFORE the secondary
                // mirror, the matrix builder and the first-person snapshot
                // consume this derived block, and publish the exact camera
                // this eye rasterizes with (compact: position +0x00, forward
                // +0x0C, up +0x18, the layout the Reach adapter reads).
                VR_DlssBeginEyeDepthWindow(policy.eye);
                if (VR_DlssWantsEyeCamera())
                {
                    float* eyeProjection = reinterpret_cast<float*>(primaryDerived + kReachDerivedProjectionOffset);
                    float jitterNdcX = 0.0f, jitterNdcY = 0.0f;
                    if (!g_reachOwnerScope.cutsceneTheater && VR_GetEyeJitter(policy.eye, jitterNdcX, jitterNdcY))
                    {
                        const float centerSign = dlss::ProjectionCenterSign(eyeProjection);
                        eyeProjection[8] += jitterNdcX * centerSign;
                        eyeProjection[9] += jitterNdcY * centerSign;
                    }
                    VrEyeCameraSample sample{};
                    memcpy(sample.position, compact + 0x00, sizeof(sample.position));
                    memcpy(sample.forward, compact + kReachCompactCameraForwardOffset, sizeof(sample.forward));
                    memcpy(sample.up, compact + 0x18, sizeof(sample.up));
                    memcpy(sample.projection, eyeProjection, sizeof(sample.projection));
                    sample.jitterNdcX = jitterNdcX;
                    sample.jitterNdcY = jitterNdcY;
                    VR_PublishEyeCamera(policy.eye, sample);
                }
                // Mirror the rebuilt primary compact + derived into the secondary
                // render pair, matching stock normal setup which mirrors both.
                memcpy(reinterpret_cast<void*>(workspace + kReachSecondaryCompactOffset), compact,
                    kReachCompactCameraBytes);
                memcpy(reinterpret_cast<void*>(workspace + kReachSecondaryDerivedOffset), primaryDerived,
                    kReachDerivedBlockSize);
                // Re-arm the render-camera owner before the state updater and the
                // render read it; the previous eye's call zeroed it (see the
                // entry check above).
                *reinterpret_cast<uintptr_t*>(reachOwnerGlobal) = entryOwner;
                g_reachHelpers.cameraState(
                    reinterpret_cast<void*>(playerView + kReachPlayerViewCameraStateOffset), compact);
                g_reachHelpers.matrix(reinterpret_cast<void*>(playerView + kReachPlayerViewCurrentMatricesOffset),
                    primaryDerived, primaryDerived + kReachDerivedProjectionOffset,
                    compact + kReachCompactRenderBoundsOffset,
                    reinterpret_cast<void*>(playerView + kReachPlayerViewProjectionOffsetPairOffset));
                // The stock outer camera-stack push committed only the centre
                // camera. Commit this rebuilt eye through Reach's exact native
                // outer callback before any world/material draw can consume
                // stale ViewVS/ViewPS state from the centre, previous eye, or
                // later weapon-dependent nested first-person pass.
                g_reachHelpers.commitOuterCamera();
                committedOuterCameraEyes |= uint32_t{1} << policy.eye;

                if (policy.writeLastWindow)
                    *reinterpret_cast<uint8_t*>(playerView + kReachLastWindowFlagOffset) = policy.lastWindowInput;
                // Re-arm once more immediately before the render in case the
                // state updater cleared or moved the owner the inner render
                // dereferences.
                *reinterpret_cast<uintptr_t*>(reachOwnerGlobal) = entryOwner;
                ReachFpCameraEyeScope& fpCameraScope = g_reachFpCameraEyeScope;
                fpCameraScope.active = false;
                fpCameraScope.generation = g_reachCamera.generation;
                fpCameraScope.eye = policy.eye;
                fpCameraScope.preparedSerial = access.preparedSerial;
                fpCameraScope.workspace = workspace;
                fpCameraScope.playerView = playerView;
                fpCameraScope.chudParityFailed = false;
                fpCameraScope.chudClass2Seen = false;
                fpCameraScope.authoredCrosshairCaptured = false;
                fpCameraScope.captureKey = 0;
                fpCameraScope.captureDraws = 0;
                memcpy(fpCameraScope.compact, compact, sizeof(fpCameraScope.compact));
                memcpy(fpCameraScope.derived, primaryDerived, sizeof(fpCameraScope.derived));
                bool renderReturned = false;
                __try
                {
                    fpCameraScope.active = true;
                    if (windState)
                    {
                        if (pass == 0)
                            windReplay.Begin(windState, ReachWindCopy);
                        else if (windReplay.Replay(windState, ReachWindCopy))
                            g_reachWindReplayPairs.fetch_add(1, std::memory_order_relaxed);
                        else
                            windReplay.Finish(windState, ReachWindCopy);
                    }
                    renderReturned = ReachCallPlayerViewWithEyeScopedSuppressions(playerView);
                }
                __finally
                {
                    fpCameraScope.active = false;
                }
                // The depth this eye rasterized is complete now, whether or
                // not the transaction below stays valid.
                VR_DlssEndEyeDepthWindow(policy.eye);
                if (!renderReturned)
                {
                    transactionValid = false;
                    break;
                }
                if (!VR_ReachCopyEye(access, policy.eye))
                {
                    transactionValid = false;
                    break;
                }
                // Publish only after this exact raster was rendered and copied.
                // The release serial makes the values visible to the OpenXR
                // submit path only for the matching prepared frame.
                g_reachRenderHalfFovX[policy.eye].store(rasterFov.horizontal, std::memory_order_relaxed);
                g_reachRenderHalfFovY[policy.eye].store(rasterFov.vertical, std::memory_order_relaxed);
                g_reachRenderFovSerial[policy.eye].store(access.preparedSerial, std::memory_order_release);
                if (policy.eye < 2)
                {
                    zoomLogHalfX[policy.eye] = rasterFov.horizontal;
                    zoomLogHalfY[policy.eye] = rasterFov.vertical;
                    zoomLogCoverX[policy.eye] = requestedFov.requiredHalfHorizontal;
                    zoomLogCoverY[policy.eye] = requestedFov.requiredHalfVertical;
                    zoomLogEyes |= 1u << policy.eye;
                }
                ++capturedEyes;
                if (pass == 0)
                {
                    memcpy(reinterpret_cast<void*>(workspace), savedWorkspace, kReachRenderScopeSnapshotSize);
                    memcpy(reinterpret_cast<void*>(playerView + kReachPvSnapshotBegin), savedPv, kReachPvSnapshotBytes);
                }
            }
            if (scopeOrder == ReachScopeOrder::AfterEyes && transactionValid && capturedEyes == 2)
            {
                // The scope pass restores the workspace and the player_view
                // snapshot to this (last eye) state itself. Keep what the eyes
                // left: the last-window byte and the render-owner global, and
                // re-commit the last eye's camera so the frame (and the next
                // one) does not end on the scope camera.
                const uint8_t lastWindowAfterEyes =
                    *reinterpret_cast<uint8_t*>(playerView + kReachLastWindowFlagOffset);
                const uintptr_t ownerAfterEyes = *reinterpret_cast<uintptr_t*>(reachOwnerGlobal);
                *reinterpret_cast<uintptr_t*>(reachOwnerGlobal) = entryOwner;
                ReachRenderScope(playerView, access);
                scopeRan = ReachScopePassRan();
                *reinterpret_cast<uint8_t*>(playerView + kReachLastWindowFlagOffset) = lastWindowAfterEyes;
                *reinterpret_cast<uintptr_t*>(reachOwnerGlobal) = entryOwner;
                unsigned char* lastDerived = reinterpret_cast<unsigned char*>(workspace + kReachPrimaryDerivedOffset);
                g_reachHelpers.cameraState(
                    reinterpret_cast<void*>(playerView + kReachPlayerViewCameraStateOffset), compact);
                g_reachHelpers.matrix(reinterpret_cast<void*>(playerView + kReachPlayerViewCurrentMatricesOffset),
                    lastDerived, lastDerived + kReachDerivedProjectionOffset, compact + kReachCompactRenderBoundsOffset,
                    reinterpret_cast<void*>(playerView + kReachPlayerViewProjectionOffsetPairOffset));
                g_reachHelpers.commitOuterCamera();
                *reinterpret_cast<uintptr_t*>(reachOwnerGlobal) = ownerAfterEyes;
            }
            completed = transactionValid && capturedEyes == 2 && (committedOuterCameraEyes & 0x3u) == 0x3u;
            ReachZoomPassLog(zoomLogHalfX, zoomLogHalfY, zoomLogCoverX, zoomLogCoverY, zoomLogEyes,
                *reinterpret_cast<const float*>(center + 0x28), access.source);
            ReachOcclusionFinish(scopeRan, capturedEyes + (scopeRan ? 1u : 0u));
            if (completed)
            {
                const uint32_t completedGeneration = g_reachCamera.generation.load(std::memory_order_relaxed);
                PublishReachOuterCameraCommitPair(completedGeneration, access.preparedSerial, committedOuterCameraEyes);
                ReachPublishCompletedFrameFeedback(completedGeneration, access.preparedSerial, g_reachOwnerScope);
            }
        }
        __finally
        {
            windReplay.Finish(windState, ReachWindCopy);
            // The first-person interpolation/palette work occurs earlier in
            // the admitted outer render. Keep that outer-owned pair scope
            // alive across both eye renders, but do not leak an eye selection
            // into the remainder of the stock outer transaction.
            g_stereoEye.store(-1, std::memory_order_release);
            ReachRestoreScope(workspace, playerView, savedWorkspace, savedPv);
        }
        return completed;
    }

    bool ReachInnerScopeMatchesLive(uintptr_t playerView, uintptr_t returnAddress)
    {
        const ReachOwnerScope& scope = g_reachOwnerScope;
        const uintptr_t base = g_reachCamera.base;
        if (g_reachNestedOuterSuppressed || !scope.active || !base || scope.playerView != playerView ||
            !scope.renderAccess || !scope.renderAccess->active || scope.cameraStackDepthBefore < -1 ||
            scope.cameraStackDepthBefore >= 3)
        {
            return false;
        }

        const int32_t depth = *reinterpret_cast<const int32_t*>(base + kReachCameraStackDepthRva);
        if (depth != scope.cameraStackDepthBefore + 1 || depth < 0 || depth > 3)
        {
            return false;
        }
        const uintptr_t topWorkspace = *reinterpret_cast<const uintptr_t*>(
            base + kReachCameraStackPointersRva + static_cast<uintptr_t>(depth) * sizeof(uintptr_t));
        return returnAddress == base + kReachPlayerViewRenderReturnRva &&
               *reinterpret_cast<const uintptr_t*>(base + kReachActiveViewRva) == playerView &&
               topWorkspace == scope.workspace &&
               *reinterpret_cast<const uintptr_t*>(scope.workspace + kReachRenderScopeSnapshotSize -
                                                   sizeof(uintptr_t)) == base + kReachCameraStackCallbackRva &&
               *reinterpret_cast<const uint32_t*>(base + kReachSelectedSpecializationRva) == 0;
    }

    void ReachPlayerViewRenderBody(uintptr_t playerView, uintptr_t returnAddress)
    {
        if (!ReachInnerScopeMatchesLive(playerView, returnAddress))
        {
            // This invocation has not crossed the exact Reach eye-ownership
            // boundary. Match Halo 3/ODST and leave unrelated native passes
            // stock, including authored states that emit no crosshair widget.
            // Only a call that passes ReachInnerScopeMatchesLive below is a
            // claimed VR transaction whose later failure must tear down.
            g_reachOrigPlayerViewRender(playerView);
            return;
        }
        if (!g_reachCamera.armed.load(std::memory_order_acquire))
        {
            // An exact inner scope exists only after the armed outer transaction
            // claimed this call tree. Mid-call revocation is terminal and may
            // never invoke the flat renderer, regardless of teardown-store order.
            Game_RejectReachAuthoredReticle(g_reachCamera.generation.load(std::memory_order_acquire),
                "core disarmed mid-call while an eye scope was claimed");
            return;
        }

        const ReachModuleEpoch epoch{g_reachCamera.base, g_reachCamera.generation};
        const ReachPreflightToken preflight = ReachRenderCandidate_GetPreflight(epoch);
        const ReachPreparedFrameToken prepared = VR_ReachPreparedFrame(epoch);
        if (!preflight.Complete() || !ReachRenderCandidate_IsPreflightCurrent(preflight) || !prepared.Ready() ||
            prepared.Serial() != g_reachOwnerScope.preparedSerial || !VR_ReachDisplayReady(epoch) ||
            !g_reachOwnerScope.renderAccess || g_reachOwnerScope.renderAccess->preparedSerial != prepared.Serial())
        {
            Game_RejectReachAuthoredReticle(
                epoch.generation, "preflight/prepared-frame/display-access proof not current");
            return;
        }

        ReachVrRenderAccess& access = *g_reachOwnerScope.renderAccess;
        bool handled = false;
        __try
        {
            handled = ReachStereoTransaction(playerView, access);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("the Reach stereo transaction faulted");
            handled = false;
        }
        if (!handled)
        {
            // Skip this frame, keep the core. ReachStereoTransaction restores
            // the workspace and player view in its own __finally, so an
            // incomplete attempt leaves no partial engine state behind, and the
            // eye serials it never published cannot be composited. A single
            // failure after minutes of correct rendering is not a reason to
            // unhook Reach and drop the player out of VR -- that is exactly the
            // over-reaction that cost 2026-07-26. Halo 3 and ODST skip and
            // recover.
            static std::atomic<uint64_t> lastSkipLogMs{0};
            const uint64_t nowMs = GetTickCount64();
            uint64_t previousMs = lastSkipLogMs.load(std::memory_order_relaxed);
            if (nowMs - previousMs >= 2000 &&
                lastSkipLogMs.compare_exchange_strong(previousMs, nowMs, std::memory_order_relaxed))
            {
                LOG("Reach frame skipped (stereo eye transaction did not "
                    "complete); the camera core stays armed and the next frame "
                    "retries");
            }
        }
    }

    // Keep the counter wrapper separate from the SEH-heavy body. The wrapper's
    // complete unwind range is also part of worker-side ingress scanning, which
    // closes the pre-counter instruction and MinHook-relay teardown windows.
    __declspec(noinline) void __fastcall ReachPlayerViewRenderDetour(uintptr_t playerView)
    {
        // Capture the engine caller before entering the helper body. Calling
        // _ReturnAddress() from that body would see this wrapper's call site
        // and make the exact retail caller gate fail permanently.
        const uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
        g_reachCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            ReachPlayerViewRenderBody(playerView, returnAddress);
        }
        __finally
        {
            g_reachCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }
} // namespace
namespace reach_bridge
{
    bool ReachBoneMatrixFinite(const BoneMatrix& matrix)
    {
        return ReachFpPackedGraphFinite(
            std::span<const float>{reinterpret_cast<const float*>(&matrix), kReachFpBoneMatrixFloatCount}, 1);
    }
} // namespace reach_bridge
namespace
{
    void PublishReachFpStatus(int code, int bodyCount, int liveCount)
    {
        const uint32_t generation = g_reachCamera.generation;
        const uint64_t key = (static_cast<uint64_t>(generation) << 32) | (static_cast<uint64_t>(code & 0xFF) << 24) |
                             (static_cast<uint64_t>(bodyCount & 0xFFF) << 12) |
                             static_cast<uint64_t>(liveCount & 0xFFF);
        if (g_reachFpStatus.key.load(std::memory_order_relaxed) == key)
            return;
        g_reachFpStatus.generation.store(generation, std::memory_order_relaxed);
        g_reachFpStatus.code.store(code, std::memory_order_relaxed);
        g_reachFpStatus.bodyCount.store(bodyCount, std::memory_order_relaxed);
        g_reachFpStatus.liveCount.store(liveCount, std::memory_order_relaxed);
        g_reachFpStatus.key.store(key, std::memory_order_release);
    }

    bool ReachLayoutsEqual(const ReachFpBodyLayout& a, const ReachFpBodyLayout& b)
    {
        return a.Valid() && b.Valid() && a.kind == b.kind && a.paletteBodyNodeCount == b.paletteBodyNodeCount &&
               a.liveSourceNodeCount == b.liveSourceNodeCount && a.rightShoulderSource == b.rightShoulderSource &&
               a.rightElbowSource == b.rightElbowSource && a.rightWristSource == b.rightWristSource &&
               a.leftShoulderSource == b.leftShoulderSource && a.leftElbowSource == b.leftElbowSource &&
               a.leftWristSource == b.leftWristSource && a.cameraControlSource == b.cameraControlSource &&
               a.rightHandSourceDescendants == b.rightHandSourceDescendants &&
               a.leftHandSourceDescendants == b.leftHandSourceDescendants &&
               a.rightControllerOwnedSourceBranch == b.rightControllerOwnedSourceBranch &&
               a.leftControllerOwnedSourceBranch == b.leftControllerOwnedSourceBranch;
    }
} // namespace
namespace reach_bridge
{
    bool ReachResolveRenderModelDescriptor(uint16_t tag, const uint8_t*& descriptor)
    {
        descriptor = nullptr;
        const uintptr_t base = g_reachCamera.base;
        if (!base)
            return false;
        const uint8_t* modelTable = nullptr;
        if (!SafeReadBytes(
                reinterpret_cast<const void*>(base + kReachRenderModelTableRva), &modelTable, sizeof(modelTable)) ||
            !modelTable)
            return false;
        uint32_t modelHandle = 0;
        if (!SafeReadBytes(modelTable + static_cast<size_t>(tag) * 8u + 4u, &modelHandle, sizeof(modelHandle)))
            return false;
        const uint8_t* blockBase = nullptr;
        if (!SafeReadBytes(reinterpret_cast<const uint8_t*>(base + kReachNodeRecordBlockTableRva) +
                               static_cast<size_t>(modelHandle >> 28) * 8u,
                &blockBase, sizeof(blockBase)) ||
            !blockBase)
            return false;
        descriptor = blockBase + static_cast<size_t>(modelHandle) * 4u;
        return true;
    }

    bool ReachReadRenderModelIdentity(uint16_t tag, uint32_t& runtimeImportChecksum, int& count)
    {
        runtimeImportChecksum = 0;
        count = 0;
        const uint8_t* descriptor = nullptr;
        if (!ReachResolveRenderModelDescriptor(tag, descriptor))
            return false;
        uint32_t rawCount = 0;
        if (!SafeReadBytes(descriptor + 0x08, &runtimeImportChecksum, sizeof(runtimeImportChecksum)) ||
            !SafeReadBytes(descriptor + 0x30, &rawCount, sizeof(rawCount)) || rawCount == 0 ||
            rawCount > kReachFpMaxSourceNodeCount)
            return false;
        count = static_cast<int>(rawCount);
        return true;
    }

    bool ReachResolvePaletteNodeCount(uint16_t tag, int& count)
    {
        count = 0;
        const uint8_t* descriptor = nullptr;
        if (!ReachResolveRenderModelDescriptor(tag, descriptor))
            return false;
        uint32_t rawCount = 0;
        if (!SafeReadBytes(descriptor + 0x30, &rawCount, sizeof(rawCount)) || rawCount == 0 ||
            rawCount > kReachFpMaxSourceNodeCount)
        {
            return false;
        }
        count = static_cast<int>(rawCount);
        return true;
    }
} // namespace reach_bridge
namespace
{
    const ReachFpLayoutCacheEntry* ReachFindFrozenLayout(int liveCount, int view, int id, int slot)
    {
        for (const ReachFpLayoutCacheEntry& entry : g_reachFpPairScope.layouts)
            if (entry.valid && entry.liveSourceCount == liveCount && entry.interpolationView == view &&
                entry.interpolationId == id && entry.interpolationSlot == slot && entry.layout.Valid())
                return &entry;
        return nullptr;
    }

    void ReachLearnLayout(const ReachFpInterpolationContext& context, uint16_t bodyTag, const ReachFpBodyLayout& layout)
    {
        if (!context.source || !layout.Valid() || !g_reachFpPairScope.armed)
            return;
        ReachFpLayoutCacheEntry* destination = nullptr;
        for (ReachFpLayoutCacheEntry& entry : g_reachFpLayoutCache)
        {
            if (entry.valid && entry.generation == g_reachFpPairScope.generation &&
                entry.liveSourceCount == context.liveSourceCount &&
                entry.interpolationView == context.interpolationView &&
                entry.interpolationId == context.interpolationId &&
                entry.interpolationSlot == context.interpolationSlot)
            {
                if (!entry.invalidateNextPair && entry.bodyTag == bodyTag && ReachLayoutsEqual(entry.layout, layout))
                    return;
                destination = &entry;
                break;
            }
            if (!entry.valid && !destination)
                destination = &entry;
        }
        if (!destination)
            destination = &g_reachFpLayoutCache[0];
        *destination = {};
        destination->valid = true;
        destination->generation = g_reachFpPairScope.generation;
        destination->liveSourceCount = context.liveSourceCount;
        destination->interpolationView = context.interpolationView;
        destination->interpolationId = context.interpolationId;
        destination->interpolationSlot = context.interpolationSlot;
        destination->bodyTag = bodyTag;
        destination->learnedPreparedSerial = g_reachFpPairScope.preparedSerial;
        destination->layout = layout;
        PublishReachFpStatus(1, static_cast<int>(layout.paletteBodyNodeCount), context.liveSourceCount);
    }

    void ReachInvalidateLayoutNextPair(const ReachFpInterpolationContext& context)
    {
        for (ReachFpLayoutCacheEntry& entry : g_reachFpLayoutCache)
            if (entry.valid && entry.generation == g_reachFpPairScope.generation &&
                entry.liveSourceCount == context.liveSourceCount &&
                entry.interpolationView == context.interpolationView &&
                entry.interpolationId == context.interpolationId &&
                entry.interpolationSlot == context.interpolationSlot)
                entry.invalidateNextPair = true;
        PublishReachFpStatus(3, 0, context.liveSourceCount);
    }
    void ReachBeginFpPairScope(uint32_t generation, uint64_t preparedSerial, const ReachVrRenderSnapshot& tracking,
        const FpExplicitPoseTargets& targets, bool showSeatedLegs)
    {
        g_reachFpPairScope = {};
        for (auto& context : g_reachFpInterpolations)
            context = {};
        g_reachFpCaptureSerial = 0;
        g_reachFpPairScope.armed = true;
        g_reachFpPairScope.showSeatedLegs = showSeatedLegs;
        g_reachFpPairScope.generation = generation;
        g_reachFpPairScope.preparedSerial = preparedSerial;
        g_reachFpPairScope.targets = targets;

        // Freeze only the exact controller sample used for this prepared
        // stereo pair. Finger posing is presentation-only and remains stock
        // whenever tracking freshness, native owner, or weapon identity is
        // ambiguous.
        if (VR_FingerPoseWanted() && tracking.preparedSerial == preparedSerial && tracking.predictedDisplayTimeNs > 0 &&
            tracking.trackingSpaceEpoch > 0)
        {
            VrContactTrackingSnapshot contact{};
            if (VR_GetContactTrackingSnapshot(contact) && contact.timeNs == tracking.predictedDisplayTimeNs &&
                contact.referenceEpoch == tracking.trackingSpaceEpoch && contact.leftHanded == tracking.leftHanded &&
                contact.handAlignment == tracking.handAlignment && contact.controllerFingers[0].valid)
            {
                uint32_t owner = UINT32_MAX, weapon = UINT32_MAX, detail = 0;
                if (ReachPrimaryWeaponEvidence(generation, owner, weapon, detail, nullptr))
                {
                    g_reachFpPairScope.fingerSampleExact = true;
                    g_reachFpPairScope.fingerWeaponEvidenceValid = true;
                    g_reachFpPairScope.secondaryWeaponPresent = (detail & 1u) != 0;
                    g_reachFpPairScope.supportFingerIsLeft = FingerAnatomicalSupportSide(tracking.handAlignment) == 0;
                    g_reachFpPairScope.supportFingerInput = contact.controllerFingers[0];
                    g_reachFpPairScope.fingerWeaponOwner = owner;
                    g_reachFpPairScope.fingerPrimaryWeapon = weapon;
                    g_reachFpPairScope.fingerLeftHanded = tracking.leftHanded;
                    g_reachFpPairScope.fingerHandAlignment = tracking.handAlignment;
                    g_reachFpPairScope.fingerTrackingSerial = contact.serial;
                    g_reachFpPairScope.fingerTrackingEpoch = contact.referenceEpoch;
                    g_reachFpPairScope.fingerDisplayTimeNs = contact.timeNs;
                }
            }
        }

        // The 82/67 output counts alone also belong to Reach's third-person
        // Spartan/Elite render models. A palette may only become the native
        // seated-leg consumer on a LATER stereo pair after its exact immutable
        // HREK checksum/count tuple has been read at this outer boundary.
        for (const ReachFpLegPaletteObservation& observation : g_reachFpLegPaletteObservations)
        {
            if (!ReachFpLegObservationCanValidate(observation.valid, observation.generation,
                    observation.observedPreparedSerial, generation, preparedSerial))
            {
                continue;
            }
            uint32_t checksum = 0;
            int nodeCount = 0;
            const bool identityRead = ReachReadRenderModelIdentity(observation.tag, checksum, nodeCount);
            const ReachFpLegPaletteKind kind =
                identityRead ? ClassifyReachFpLegPalette(checksum, nodeCount) : ReachFpLegPaletteKind::None;
            if (kind != ReachFpLegPaletteKind::None && nodeCount == observation.nodeCount)
            {
                g_reachFpLegPaletteCache = {
                    true, generation, observation.observedPreparedSerial, observation.tag, kind};
                break;
            }
            g_reachFpLegPaletteIdentityRejects.fetch_add(1, std::memory_order_relaxed);
        }
        for (auto& observation : g_reachFpLegPaletteObservations)
            observation = {};

        if (g_reachFpLegPaletteCache.valid && g_reachFpLegPaletteCache.generation == generation &&
            preparedSerial > g_reachFpLegPaletteCache.learnedPreparedSerial)
        {
            uint32_t checksum = 0;
            int nodeCount = 0;
            const bool identityRead = ReachReadRenderModelIdentity(g_reachFpLegPaletteCache.tag, checksum, nodeCount);
            const ReachFpLegPaletteKind kind =
                identityRead ? ClassifyReachFpLegPalette(checksum, nodeCount) : ReachFpLegPaletteKind::None;
            if (kind == g_reachFpLegPaletteCache.kind && kind != ReachFpLegPaletteKind::None)
            {
                g_reachFpPairScope.legPaletteValid = true;
                g_reachFpPairScope.legPaletteTag = g_reachFpLegPaletteCache.tag;
                g_reachFpPairScope.legPaletteKind = kind;
            }
            else
            {
                g_reachFpLegPaletteCache = {};
                g_reachFpLegPaletteIdentityRejects.fetch_add(1, std::memory_order_relaxed);
            }
        }
        else if (g_reachFpLegPaletteCache.valid && g_reachFpLegPaletteCache.generation != generation)
        {
            g_reachFpLegPaletteCache = {};
        }

        size_t frozen = 0;
        for (ReachFpLayoutCacheEntry& entry : g_reachFpLayoutCache)
        {
            if (entry.valid && (entry.generation != generation || entry.invalidateNextPair))
                entry = {};
            if (DecideReachFpPairLayout(entry.valid, entry.invalidateNextPair, entry.generation,
                    entry.learnedPreparedSerial, generation, preparedSerial) == ReachFpPairLayoutDecision::Active &&
                frozen < kReachFpLayoutCacheCapacity)
                g_reachFpPairScope.layouts[frozen++] = entry;
        }
        g_fpStereoSolveScope = {};
        g_fpStereoSolveScope.armed = true;
        if (targets.centerRootValid)
        {
            g_fpStereoSolveScope.centerRootValid = true;
            g_fpStereoSolveScope.centerRoot = targets.centerRoot;
        }
    }

    void ReachEndFpPairScope()
    {
        g_stereoEye.store(-1, std::memory_order_release);
        g_fpStereoSolveScope = {};
        for (auto& context : g_reachFpInterpolations)
            context = {};
        g_reachFpCaptureSerial = 0;
        g_reachFpPairScope = {};
    }

    void ReachApplyFreeSupportFingerPose(
        uint16_t tag, int paletteCount, const ReachFpInterpolationContext& context, BoneMatrix* destination) noexcept
    {
        if (!destination || !VR_FingerPoseWanted() || !g_reachFpPairScope.armed ||
            !g_reachFpPairScope.fingerSampleExact || !g_reachFpPairScope.fingerWeaponEvidenceValid ||
            context.generation != g_reachFpPairScope.generation ||
            context.preparedSerial != g_reachFpPairScope.preparedSerial || !context.valid || !context.transformed ||
            context.interpolationView != 0 || context.interpolationSlot != 0 ||
            g_reachFpPairScope.secondaryWeaponPresent)
        {
            g_reachFingerPoseRefused.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        const bool supportAttached = context.targets.supportGripResolved ? context.targets.supportGripAttached
                                                                         : context.targets.twoHandAimActive;
        if (!reach_fingers::FreeSupportPoseAdmitted(true, true, true, g_reachFpPairScope.secondaryWeaponPresent,
                supportAttached, context.targets.twoHandAimActive, g_reachFpPairScope.fingerSampleExact,
                context.interpolationSlot == 0))
        {
            g_reachFingerPoseRefused.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        uint32_t owner = UINT32_MAX, weapon = UINT32_MAX, detail = 0;
        VrContactTrackingSnapshot currentFingerSample{};
        if (!ReachPrimaryWeaponEvidence(context.generation, owner, weapon, detail, nullptr) ||
            owner != g_reachFpPairScope.fingerWeaponOwner || weapon != g_reachFpPairScope.fingerPrimaryWeapon ||
            ((detail & 1u) != 0) != g_reachFpPairScope.secondaryWeaponPresent ||
            !VR_GetContactTrackingSnapshot(currentFingerSample) ||
            currentFingerSample.serial != g_reachFpPairScope.fingerTrackingSerial ||
            currentFingerSample.timeNs != g_reachFpPairScope.fingerDisplayTimeNs ||
            currentFingerSample.referenceEpoch != g_reachFpPairScope.fingerTrackingEpoch ||
            currentFingerSample.leftHanded != g_reachFpPairScope.fingerLeftHanded ||
            currentFingerSample.handAlignment != g_reachFpPairScope.fingerHandAlignment ||
            !currentFingerSample.controllerFingers[0].valid ||
            currentFingerSample.controllerFingers[0].trigger != g_reachFpPairScope.supportFingerInput.trigger ||
            currentFingerSample.controllerFingers[0].grip != g_reachFpPairScope.supportFingerInput.grip)
        {
            g_reachFingerPoseRefused.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        uint32_t checksum = 0;
        int verifiedCount = 0;
        if (!ReachReadRenderModelIdentity(tag, checksum, verifiedCount) || verifiedCount != paletteCount ||
            (paletteCount != 47 && paletteCount != 41))
        {
            g_reachFingerPoseRefused.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        AnatomicalPalmMarkers markers{};
        if (!ReachAnatomicalPalmMarkers(checksum, paletteCount, markers))
        {
            g_reachFingerPoseRefused.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        const unsigned side = g_reachFpPairScope.supportFingerIsLeft ? 0u : 1u;
        const int wristNode = side == 0 ? markers.leftNode : markers.rightNode;
        if (wristNode < 0 || wristNode >= paletteCount)
        {
            g_reachFingerPoseRefused.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        Halo4FloatingTransform wrist{}, palm{};
        wrist.scale = destination[wristNode].scale;
        std::memcpy(wrist.rotation, destination[wristNode].rotation, sizeof(wrist.rotation));
        std::memcpy(wrist.translation, destination[wristNode].translation, sizeof(wrist.translation));
        if (!Halo4FloatingTransformValid(wrist) ||
            !Halo4ComposeFloatingTransforms(wrist, side == 0 ? markers.left : markers.right, palm))
        {
            g_reachFingerPoseRefused.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        const float palmDown[3]{-palm.rotation[6], -palm.rotation[7], -palm.rotation[8]};
        BoneMatrix before[reach_fingers::kSpartanNodeCount]{};
        BoneMatrix candidate[reach_fingers::kSpartanNodeCount]{};
        const size_t bytes = size_t(paletteCount) * sizeof(BoneMatrix);
        if (!SafeReadBytes(destination, before, bytes))
        {
            g_reachFingerPoseWriteFailures.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        const bool solved = reach_fingers::Apply(checksum, static_cast<unsigned>(paletteCount), side, before, palmDown,
            g_reachFpPairScope.supportFingerInput, candidate);
        if (!solved)
        {
            g_reachFingerPoseRefused.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (!SafeWriteBytes(destination, candidate, bytes))
        {
            if (!SafeWriteBytes(destination, before, bytes))
                g_reachFingerPoseRollbackFailures.fetch_add(1, std::memory_order_relaxed);
            g_reachFingerPoseWriteFailures.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        g_reachFingerPoseApplied.fetch_add(1, std::memory_order_relaxed);
    }

    void ReachObserveFpLegPalette(uint16_t tag, int nodeCount)
    {
        if (!g_reachFpPairScope.armed ||
            (nodeCount != kReachSpartanFpLegNodeCount && nodeCount != kReachEliteFpLegNodeCount))
        {
            return;
        }
        ReachFpLegPaletteObservation* destination = nullptr;
        for (auto& observation : g_reachFpLegPaletteObservations)
        {
            if (observation.valid && observation.generation == g_reachFpPairScope.generation &&
                observation.observedPreparedSerial == g_reachFpPairScope.preparedSerial && observation.tag == tag)
            {
                return;
            }
            if (!observation.valid && !destination)
                destination = &observation;
        }
        if (!destination)
            return;
        *destination = {true, g_reachFpPairScope.generation, g_reachFpPairScope.preparedSerial, tag, nodeCount};
        g_reachFpLegPaletteObservationCount.fetch_add(1, std::memory_order_relaxed);
    }

    bool ReachBuildCenterFpRoot(const unsigned char* compact, const float* positionOverride, BoneMatrix& out)
    {
        if (!compact)
            return false;
        const float* pos = reinterpret_cast<const float*>(compact + 0x00);
        const float* fwdIn = reinterpret_cast<const float*>(compact + 0x0C);
        const float* upIn = reinterpret_cast<const float*>(compact + 0x18);
        float fwd[3] = {fwdIn[0], fwdIn[1], fwdIn[2]};
        float up[3] = {upIn[0], upIn[1], upIn[2]};
        const float fl = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
        if (!isfinite(fl) || fl < 1e-4f)
            return false;
        for (float& component : fwd)
            component /= fl;
        float left[3] = {
            up[1] * fwd[2] - up[2] * fwd[1], up[2] * fwd[0] - up[0] * fwd[2], up[0] * fwd[1] - up[1] * fwd[0]};
        const float ll = sqrtf(left[0] * left[0] + left[1] * left[1] + left[2] * left[2]);
        if (!isfinite(ll) || ll < 1e-4f)
            return false;
        for (float& component : left)
            component /= ll;
        up[0] = fwd[1] * left[2] - fwd[2] * left[1];
        up[1] = fwd[2] * left[0] - fwd[0] * left[2];
        up[2] = fwd[0] * left[1] - fwd[1] * left[0];
        out = {};
        out.scale = 1.0f;
        memcpy(out.rotation, fwd, sizeof(fwd));
        memcpy(out.rotation + 3, left, sizeof(left));
        memcpy(out.rotation + 6, up, sizeof(up));
        memcpy(out.translation, positionOverride ? positionOverride : pos, sizeof(out.translation));
        return ReachBoneMatrixFinite(out);
    }

    bool ReachBuildPreparedControllerTarget(const ReachVrRenderSnapshot& tracking, bool left,
        const float gameplayBase[3], BoneMatrix& out, float& meshScale,
        contact_melee::TrackingToWorld* contactSpace = nullptr,
        contact_melee::TrackingToWorld* contactController = nullptr)
    {
        const bool valid = left ? tracking.leftControllerValid : tracking.rightAimValid;
        if (!valid || !gameplayBase)
            return false;
        float q[4], p[3];
        if (left)
        {
            memcpy(q, tracking.leftControllerOrientation, sizeof(q));
            memcpy(p, tracking.leftControllerPosition, sizeof(p));
        }
        else
        {
            memcpy(q, tracking.rightAimOrientation, sizeof(q));
            memcpy(p, tracking.rightAimPosition, sizeof(p));
        }
        float ql = 0.0f;
        for (float component : q)
        {
            if (!isfinite(component))
                return false;
            ql += component * component;
        }
        ql = sqrtf(ql);
        if (!isfinite(ql) || ql < 1e-5f)
            return false;
        for (float& component : q)
            component /= ql;
        if (contactController)
        {
            // Contact motion follows the physical hand. The visual two-hand
            // solver still controls the mesh, and invalid contact tracking
            // cannot reject the existing first-person render transaction.
            *contactController = {};
            if ((!left && !tracking.rightPhysicalValid) ||
                !contactController->SetPose(left ? q : tracking.rightPhysicalOrientation, p))
                contactController->unitsPerMetre = 0;
        }
        float hullYaw = 0.0f, hullPitch = 0.0f;
        const bool followValid = Halo3ReadRollStableFollow(hullYaw, hullPitch);
        float basis[9];
        BuildTrackedGameBasisFromFrame(q, false, followValid, hullYaw, hullPitch, basis);
        if (left)
        {
            float mount[9], trimmed[9];
            BasisFromAngles(-g_config.gun_yaw_deg * 0.0174533f, g_config.gun_pitch_deg * 0.0174533f,
                -g_config.gun_roll_deg * 0.0174533f, mount);
            MultiplyBases(basis, mount, trimmed);
            memcpy(basis, trimmed, sizeof(basis));
        }
        else if (g_config.barrel_pitch_deg != 0.0f || g_config.barrel_yaw_deg != 0.0f ||
                 g_config.barrel_roll_deg != 0.0f)
        {
            // Mesh-only barrel trim (per title profile); aim ray untouched.
            float mount[9], trimmed[9];
            BasisFromAngles(-g_config.barrel_yaw_deg * 0.0174533f, g_config.barrel_pitch_deg * 0.0174533f,
                -g_config.barrel_roll_deg * 0.0174533f, mount);
            MultiplyBases(basis, mount, trimmed);
            bool finite = true;
            for (int j = 0; j < 9; ++j)
                if (!isfinite(trimmed[j]))
                {
                    finite = false;
                    break;
                }
            if (finite)
                memcpy(basis, trimmed, sizeof(basis));
        }
        const float dx = p[0] - g_headPosRef[0];
        const float dy = p[1] - g_headPosRef[1];
        const float dz = p[2] - g_headPosRef[2];
        const float sh = sinf(g_headYawRef), ch = cosf(g_headYawRef);
        const float roomForward = dx * sh - dz * ch;
        const float roomRight = dx * ch + dz * sh;
        const float cg = cosf(g_gameYawRef), sg = sinf(g_gameYawRef);
        const float scale = kReachWorldUnitsPerMeter;
        float offset[3]{};
        float positionBasis[9]{};
        const float positionLocal[3] = {roomForward, -roomRight, dy};
        float positionWorld[3]{};
        const bool positionFollows =
            followValid &&
            Halo3ComposeFollowBasisWithRoll(hullYaw, hullPitch, g_gameYawRef, 0.0f, 0.0f, 0.0f, positionBasis) &&
            Halo3TransformBasisVector(positionBasis, positionLocal, positionWorld);
        if (positionFollows)
        {
            offset[0] = positionWorld[0] * scale;
            offset[1] = positionWorld[1] * scale;
            offset[2] = positionWorld[2] * scale;
        }
        else
        {
            offset[0] = (cg * roomForward + sg * roomRight) * scale;
            offset[1] = (sg * roomForward - cg * roomRight) * scale;
            offset[2] = dy * scale;
        }
        if (contactSpace)
        {
            // Invert the SAME position transform used by this hand. Native
            // geometry can then be compared in physical tracking metres.
            if (!positionFollows)
            {
                const float flat[9]{cg, sg, 0, -sg, cg, 0, 0, 0, 1};
                memcpy(positionBasis, flat, sizeof(flat));
            }
            *contactSpace = {};
            contactSpace->unitsPerMetre = scale;
            contactSpace->axis[0] = {positionBasis[0] * sh - positionBasis[3] * ch,
                positionBasis[1] * sh - positionBasis[4] * ch, positionBasis[2] * sh - positionBasis[5] * ch};
            contactSpace->axis[1] = {positionBasis[6], positionBasis[7], positionBasis[8]};
            contactSpace->axis[2] = {-positionBasis[0] * ch - positionBasis[3] * sh,
                -positionBasis[1] * ch - positionBasis[4] * sh, -positionBasis[2] * ch - positionBasis[5] * sh};
            const auto reference = contactSpace->World({g_headPosRef[0], g_headPosRef[1], g_headPosRef[2]});
            contactSpace->origin = {
                gameplayBase[0] - reference.x, gameplayBase[1] - reference.y, gameplayBase[2] - reference.z};
        }
        const float standoff =
            (left ? Clamp(g_config.left_hand_forward_m, -0.15f, 0.30f) : Clamp(g_config.gun_forward_m, -0.3f, 0.5f)) *
            scale;
        const float rightOff = !left ? Clamp(g_config.gun_right_m, -0.3f, 0.3f) * scale : 0.0f;
        const float upOff = !left ? Clamp(g_config.gun_up_m, -0.3f, 0.3f) * scale : 0.0f;
        out = {};
        out.scale = 1.0f;
        memcpy(out.rotation, basis, sizeof(basis));
        for (int axis = 0; axis < 3; ++axis)
            out.translation[axis] = gameplayBase[axis] + offset[axis] + basis[axis] * standoff +
                                    basis[3 + axis] * rightOff + basis[6 + axis] * upOff;
        meshScale = left ? Clamp(g_config.left_hand_scale, 0.3f, 3.0f) : Clamp(g_config.gun_scale, 0.3f, 3.0f);
        return ReachBoneMatrixFinite(out) && isfinite(meshScale);
    }

    bool ReachAlignRightTargetToAuthoredBarrel(
        const BoneMatrix& baseTarget, const BoneMatrix& authoredWrist, BoneMatrix& alignedTarget)
    {
        float barrelLocal[3] = {authoredWrist.rotation[0], authoredWrist.rotation[3], authoredWrist.rotation[6]};
        const float length =
            sqrtf(barrelLocal[0] * barrelLocal[0] + barrelLocal[1] * barrelLocal[1] + barrelLocal[2] * barrelLocal[2]);
        if (!isfinite(length) || length < 1e-4f)
            return false;
        for (float& component : barrelLocal)
            component /= length;
        float worldBarrel[3] = {0.0f, 0.0f, 0.0f};
        for (int column = 0; column < 3; ++column)
            for (int row = 0; row < 3; ++row)
                worldBarrel[row] += baseTarget.rotation[column * 3 + row] * barrelLocal[column];
        const float ray[3] = {baseTarget.rotation[0], baseTarget.rotation[1], baseTarget.rotation[2]};
        float swing[9], rotated[9];
        ShortestArcRotation(worldBarrel, ray, swing);
        MultiplyBases(swing, baseTarget.rotation, rotated);
        alignedTarget = baseTarget;
        memcpy(alignedTarget.rotation, rotated, sizeof(rotated));
        return ReachBoneMatrixFinite(alignedTarget);
    }

    // Reach's official Spartan/Elite body maps resolve the left wrist to source
    // node 11 and publish the complete source-space left-hand descendant mask.
    // The shared rigid (arm_ik=0) reconstruction intentionally carries the
    // whole weapon assembly from the right wrist; that is correct for the gun.
    // HREK proves the visible left-hand skin is also weighted across its hidden
    // upper-arm/forearm/humerus/radius branch. Apply the controller delta only
    // to the visible hand. The later Reach-only presentation pass co-locates
    // those hidden influence records at the solved wrist before collapsing
    // them, so their geometry disappears without pulling the glove back toward
    // four separate arm pivots. Live animation/marker graphs remain untouched.
    bool ReachBindFloatingLeftHandToController(const BoneMatrix& renderRoot, const FpInterpolationContext& fp,
        uint64_t leftControllerOwnedSourceBranch, const FpExplicitPoseTargets& targets)
    {
        if (!targets.leftWristValid)
            return true;
        if (!ReachBoneMatrixFinite(renderRoot) || !ReachBoneMatrixFinite(targets.leftWrist) || fp.count <= 0 ||
            fp.count > static_cast<int>(kReachFpMaxSourceNodeCount) || fp.lWrist < 0 || fp.lWrist >= fp.count ||
            fp.lWrist >= 64 || !(fp.lWristDescendants & (uint64_t{1} << fp.lWrist)) ||
            (leftControllerOwnedSourceBranch & fp.lWristDescendants) != fp.lWristDescendants ||
            (leftControllerOwnedSourceBranch & fp.wristDescendants) != 0 ||
            (fp.count < 64 && (leftControllerOwnedSourceBranch >> fp.count) != 0))
        {
            return false;
        }

        BoneMatrix currentWristWorld{}, inverseCurrentWrist{}, worldDelta{};
        BoneMatrix inverseRoot{}, deltaRoot{}, recordDelta{};
        if (!ComposeBoneMatrices(renderRoot, g_fpPaletteScratch[fp.lWrist], currentWristWorld) ||
            !InvertBoneMatrix(currentWristWorld, inverseCurrentWrist) ||
            !ComposeBoneMatrices(targets.leftWrist, inverseCurrentWrist, worldDelta) ||
            !InvertBoneMatrix(renderRoot, inverseRoot) || !ComposeBoneMatrices(worldDelta, renderRoot, deltaRoot) ||
            !ComposeBoneMatrices(inverseRoot, deltaRoot, recordDelta))
        {
            return false;
        }

        for (int node = 0; node < fp.count && node < 64; ++node)
        {
            if (!(fp.lWristDescendants & (uint64_t{1} << node)))
                continue;
            BoneMatrix transformed{};
            if (!ComposeBoneMatrices(recordDelta, g_fpPaletteScratch[node], transformed) ||
                !ReachBoneMatrixFinite(transformed))
            {
                return false;
            }
            g_fpPaletteScratch[node] = transformed;
        }

        BoneMatrix verifiedWrist{};
        if (!ComposeBoneMatrices(renderRoot, g_fpPaletteScratch[fp.lWrist], verifiedWrist) ||
            !ReachBoneMatrixFinite(verifiedWrist))
        {
            return false;
        }
        for (int axis = 0; axis < 3; ++axis)
            if (fabsf(verifiedWrist.translation[axis] - targets.leftWrist.translation[axis]) > 0.01f)
                return false;
        return true;
    }
} // namespace
namespace reach_bridge
{
    constinit std::atomic<uint64_t> g_reachAnatomicalApplications{0};
    constinit std::atomic<uint64_t> g_reachAnatomicalRefusals{0};
} // namespace reach_bridge
namespace
{
    bool ReachRouteLeftHandedPalette(uint16_t tag, const int32_t* boneMap, const FpInterpolationContext& fp,
        const BoneMatrix& root, const FpExplicitPoseTargets& rawTargets)
    {
        if (!rawTargets.rightWristValid || !rawTargets.leftWristValid || fp.count <= 0 ||
            fp.count > int(kReachFpMaxSourceNodeCount) || fp.heldObjectStart <= 0 || fp.heldObjectStart > 64 ||
            (fp.heldObjectStart < 64 && ((fp.wristDescendants | fp.lWristDescendants) >> fp.heldObjectStart)))
            return false;
        uint32_t checksum = 0;
        int bodyCount = 0;
        AnatomicalPalmMarkers palms{};
        if (!ReachReadRenderModelIdentity(tag, checksum, bodyCount) || bodyCount != fp.heldObjectStart ||
            !ReachAnatomicalPalmMarkers(checksum, bodyCount, palms))
            return false;
        int32_t remap[64]{};
        if (!boneMap || !SafeReadBytes(boneMap, remap, size_t(bodyCount) * sizeof(*remap)) ||
            remap[palms.rightNode] != fp.wrist || remap[palms.leftNode] != fp.lWrist)
            return false;
        const auto value = [](const BoneMatrix& input)
        {
            Halo4FloatingTransform out{};
            out.scale = input.scale;
            std::memcpy(out.rotation, input.rotation, sizeof(out.rotation));
            std::memcpy(out.translation, input.translation, sizeof(out.translation));
            return out;
        };
        Halo4FloatingTransform palette[kReachFpMaxSourceNodeCount];
        for (int i = 0; i < fp.count; ++i)
            palette[i] = value(g_fpPaletteScratch[i]);
        if (!RouteLeftHandedFloatingPalette(palette, size_t(fp.count), fp.wrist, fp.wristDescendants, fp.lWrist,
                fp.lWristDescendants, value(root), palms.right, palms.left))
            return false;
        for (int i = 0; i < fp.count; ++i)
        {
            g_fpPaletteScratch[i].scale = palette[i].scale;
            std::memcpy(g_fpPaletteScratch[i].rotation, palette[i].rotation, sizeof(palette[i].rotation));
            std::memcpy(g_fpPaletteScratch[i].translation, palette[i].translation, sizeof(palette[i].translation));
        }
        return true;
    }

    void ReachCaptureFpInterpolation(int view, int id, int slot, bool result, BoneMatrix** outBones, int* outCount,
        uint32_t muzzleUnit, uint32_t muzzleWeapon)
    {
        if (slot != 0 || g_reachNestedOuterSuppressed || !result || !outBones || !outCount || !*outBones ||
            !g_reachFpPairScope.armed || g_reachFpPairScope.generation != g_reachCamera.generation ||
            !g_reachCamera.armed.load(std::memory_order_acquire) || !g_enabled.load(std::memory_order_relaxed) ||
            !g_vrAim.load(std::memory_order_relaxed))
            return;
        const int count = *outCount;
        if (count <= 0 || count > static_cast<int>(kReachFpMaxSourceNodeCount))
            return;

        // Retail pairs each interpolation with its palette immediately. Retain
        // one bounded context per outstanding transaction and let the palette
        // consume the newest exact source-pointer match, as H3/ODST do.
        ReachFpInterpolationContext* destination = nullptr;
        for (auto& candidate : g_reachFpInterpolations)
            if (!candidate.valid)
            {
                destination = &candidate;
                break;
            }
        if (!destination)
            return;
        *destination = {};
        ReachFpInterpolationContext& context = *destination;
        context.valid = true;
        context.generation = g_reachFpPairScope.generation;
        context.preparedSerial = g_reachFpPairScope.preparedSerial;
        context.captureSerial = ++g_reachFpCaptureSerial;
        context.source = *outBones;
        context.liveSourceCount = count;
        context.interpolationView = view;
        context.interpolationId = id;
        context.interpolationSlot = slot;
        uint32_t currentUnit = UINT32_MAX, currentWeapon = UINT32_MAX;
        if (ReachCaptureMuzzleOwner(view, slot, currentUnit, currentWeapon) && muzzleUnit == currentUnit &&
            muzzleWeapon == currentWeapon)
        {
            context.muzzleUnit = muzzleUnit;
            context.muzzleWeapon = muzzleWeapon;
        }

        const ReachFpLayoutCacheEntry* frozen = ReachFindFrozenLayout(count, view, id, slot);
        if (!frozen)
            return;
        context.layout = frozen->layout;
        context.bodyTag = frozen->bodyTag;
        if (!context.layout.Valid() || !g_reachFpPairScope.targets.centerRootValid ||
            !g_reachFpPairScope.targets.rightWristValid ||
            !ReachBoneMatrixFinite(g_reachFpPairScope.targets.centerRoot) ||
            !ReachBoneMatrixFinite(g_reachFpPairScope.targets.rightWrist) ||
            !isfinite(g_reachFpPairScope.targets.rightScale) || g_reachFpPairScope.targets.rightScale <= 0.0f ||
            (g_reachFpPairScope.targets.leftWristValid &&
                (!ReachBoneMatrixFinite(g_reachFpPairScope.targets.leftWrist) ||
                    !isfinite(g_reachFpPairScope.targets.leftScale) || g_reachFpPairScope.targets.leftScale <= 0.0f)))
            return;
        const size_t liveBytes = static_cast<size_t>(count) * sizeof(BoneMatrix);
        if (!SafeReadBytes(context.source, context.untouchedLive, liveBytes))
            return;
        for (int node = 0; node < count; ++node)
            if (!ReachBoneMatrixFinite(context.untouchedLive[node]))
                return;
        context.targets = g_reachFpPairScope.targets;
        // Persistent support grip: the pair's frozen owner trust travels with
        // this invocation's targets, so the palette below presents the same
        // decision the FP hook just resolved. A resolved pair that is NOT the
        // relationship owner re-seats the visible weapon hand on the ordinary
        // calibrated one-hand target (the stale-owner fallback); an unresolved
        // pair (feature off) leaves the targets byte-identical to the base.
        context.targets.supportGripResolved = g_reachFpPairScope.supportGripResolved;
        context.targets.supportGripAttached = g_reachFpPairScope.supportGripAttached;
        if (g_reachFpPairScope.supportGripResolved && !g_reachFpPairScope.supportGripAttached &&
            context.targets.rightOneHandWristValid)
        {
            context.targets.rightWrist = context.targets.rightOneHandWrist;
            context.targets.rightScale = context.targets.rightOneHandScale;
        }

        // Match the accepted H3/ODST split exactly: the live interpolation bank
        // is only for marker/muzzle/attachment consumers and receives one rigid
        // controller transform. Every visible palette below is reconstructed
        // independently from untouchedLive into private scratch.
        // Prepared wrists are already absolute world targets (pre-head gameplay
        // base + tracked controller displacement). Match H3/ODST by using the
        // center root only for world/record conversion; adding its head
        // translation again would make the assembly drift on a physical turn.
        FpExplicitPoseTargets markerTargets = context.targets;
        BoneMatrix alignedRight{};
        if (!ReachAlignRightTargetToAuthoredBarrel(
                markerTargets.rightWrist, context.untouchedLive[context.layout.rightWristSource], alignedRight))
            return;
        if (!ApplyControllerToMarkerBonesWithTarget(markerTargets.centerRoot, alignedRight, markerTargets.rightScale,
                context.source, count, context.layout.rightWristSource))
        {
            SafeWriteBytes(context.source, context.untouchedLive, liveBytes);
            return;
        }
        for (int node = 0; node < count; ++node)
        {
            BoneMatrix checked{};
            if (!SafeReadBytes(context.source + node, &checked, sizeof(checked)) || !ReachBoneMatrixFinite(checked))
            {
                SafeWriteBytes(context.source, context.untouchedLive, liveBytes);
                return;
            }
        }
        context.transformed = true;
    }
    // ---- Weapon-order diagnostic tranche (read-only evidence) ----
    void PublishReachFpDiagnostic(WeaponOrderEventKind kind, WeaponOrderEventStatus status, uint64_t preparedSerial,
        uint32_t unit, uint32_t weapon, uint64_t aux0, uint64_t aux1) noexcept
    {
        if (!Telemetry_WeaponEventsAccepting())
            return;
        Telemetry_PublishWeaponEvent(static_cast<uint8_t>(kind), static_cast<uint8_t>(status),
            static_cast<uint8_t>(GameTitle::HaloReach), g_reachCamera.generation.load(std::memory_order_acquire),
            preparedSerial, unit, weapon, aux0, aux1);
    }

    __declspec(noinline) bool __fastcall ReachFpInterpolate(
        int view, int id, int slot, BoneMatrix** outBones, int* outCount)
    {
        g_reachCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        // Diagnostic-only FP entry + ungated before-read (no gameplay effect).
        uint32_t diagnosticBeforeUnit = UINT32_MAX;
        uint32_t diagnosticBeforeWeapon = UINT32_MAX;
        uint32_t diagnosticBeforeDetail = 0;
        const bool diagnosticArmed = Telemetry_WeaponEventsAccepting() && slot == 0 && view == 0;
        if (diagnosticArmed)
        {
            Game_DiagnosticNoteFpThread(GameTitle::HaloReach);
            PublishReachFpDiagnostic(WeaponOrderEventKind::FpEntry, WeaponOrderEventStatus::NoObservation,
                g_reachFpPairScope.preparedSerial, UINT32_MAX, UINT32_MAX, uint64_t(slot), uint64_t(view));
            uint32_t probeUnit = UINT32_MAX, probeWeapon = UINT32_MAX;
            uint32_t probeDetail = 0;
            if (Game_DiagnosticReadPrimaryWeapon(GameTitle::HaloReach, probeUnit, probeWeapon, probeDetail))
            {
                diagnosticBeforeUnit = probeUnit;
                diagnosticBeforeWeapon = probeWeapon;
                diagnosticBeforeDetail = probeDetail;
            }
        }
        // Persistent support grip production evidence (root slot-0 invocation
        // of the stereo pair only: whichever slot-0 view interpolates first
        // resolves, the other eye reuses the pair's frozen decision, and a
        // later invocation of the same pair must not overwrite it). The
        // before-read is taken here; the after-read lands IMMEDIATELY after the
        // native original and BEFORE ReachCaptureFpInterpolation copies the
        // support-dependent targets into the per-invocation context.
        // Read-only with respect to the durable relationship; PG
        // off: no read, no publication, no receipt.
        const bool supportRootInvocation = g_config.persistent_support_grip && slot == 0 && g_reachFpPairScope.armed &&
                                           !g_reachFpPairScope.supportGripResolved;
        PrimaryWeaponEvidence supportBefore{};
        const bool supportBeforeOk =
            supportRootInvocation && Game_ReadPrimaryWeaponEvidence(GameTitle::HaloReach, supportBefore);
        bool result = false;
        uint32_t muzzleUnit = UINT32_MAX, muzzleWeapon = UINT32_MAX;
        (void)ReachCaptureMuzzleOwner(view, slot, muzzleUnit, muzzleWeapon);
        __try
        {
            ReachFpInterpolateFn original = g_reachOrigFpInterpolate;
            if (original)
                result = original(view, id, slot, outBones, outCount);
            if (supportRootInvocation)
            {
                PrimaryWeaponEvidence supportAfter{};
                const bool supportAfterOk = Game_ReadPrimaryWeaponEvidence(GameTitle::HaloReach, supportAfter);
                const SupportInvocationResolution resolution = Game_ResolveSupportInvocation(
                    GameTitle::HaloReach, supportBefore, supportBeforeOk, supportAfter, supportAfterOk, true);
                g_reachFpPairScope.supportGripAttached = resolution.trusted;
                g_reachFpPairScope.supportGripResolved = true;
                // Freeze the title-native invocation receipt for this
                // pair so the presented-ray producer and its consumers can
                // require the consuming invocation's own owner proof instead of
                // trusting the latest durable cache.
                support_grip::SupportInvocationReceipt receipt{};
                receipt.resolved = true;
                receipt.title = GameTitle::HaloReach;
                receipt.generation = resolution.generation;
                receipt.serial = g_reachFpPairScope.preparedSerial;
                receipt.evidence = resolution.evidence;
                receipt.producerAgreement = resolution.producerAgreement;
                receipt.owner = resolution.owner;
                receipt.relationshipReadable = resolution.relationshipReadable;
                receipt.relationshipEngaged = resolution.relationshipEngaged;
                receipt.relationshipEpoch = resolution.relationshipEpoch;
                VR_PublishSupportInvocationReceipt(receipt);
            }
            ReachCaptureFpInterpolation(view, id, slot, result, outBones, outCount, muzzleUnit, muzzleWeapon);
        }
        __finally
        {
            g_reachCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
        // Diagnostic-only stable commit, mirroring the existing ownership
        // stable point ungated: inventory identity plus engine FP record
        // agreement (bit2) must survive the interpolation unchanged.
        if (diagnosticArmed && result && diagnosticBeforeWeapon != UINT32_MAX && (diagnosticBeforeDetail & 0x4u) != 0)
        {
            uint32_t afterUnit = UINT32_MAX, afterWeapon = UINT32_MAX;
            uint32_t afterDetail = 0;
            if (Game_DiagnosticReadPrimaryWeapon(GameTitle::HaloReach, afterUnit, afterWeapon, afterDetail) &&
                (afterDetail & 0x4u) != 0 && afterUnit == diagnosticBeforeUnit && afterWeapon == diagnosticBeforeWeapon)
                PublishReachFpDiagnostic(WeaponOrderEventKind::FpWeaponCommit, WeaponOrderEventStatus::Success,
                    g_reachFpPairScope.preparedSerial, afterUnit, afterWeapon, uint64_t(slot), afterDetail);
        }
        return result;
    }

    bool ReachRestoreFpLiveGraph(ReachFpInterpolationContext& context)
    {
        if (!context.transformed)
            return true;
        if (!context.source || context.liveSourceCount <= 0 ||
            context.liveSourceCount > static_cast<int>(kReachFpMaxSourceNodeCount))
            return false;
        const size_t bytes = static_cast<size_t>(context.liveSourceCount) * sizeof(BoneMatrix);
        if (!SafeWriteBytes(context.source, context.untouchedLive, bytes))
            return false;
        context.transformed = false;
        return true;
    }

    bool ReachBuildCenteredNativeFpPalette(const BoneMatrix& paletteRoot, const BoneMatrix& centerRoot,
        const BoneMatrix* untouchedSource, int sourceCount, const BoneMatrix*& replacement)
    {
        replacement = untouchedSource;
        if (!untouchedSource || sourceCount <= 0 || sourceCount > static_cast<int>(kReachFpMaxSourceNodeCount) ||
            !ReachBoneMatrixFinite(paletteRoot) || !ReachBoneMatrixFinite(centerRoot))
        {
            return false;
        }
        BoneMatrix inverseRoot{}, centerToPalette{};
        if (!InvertBoneMatrix(paletteRoot, inverseRoot) ||
            !ComposeBoneMatrices(inverseRoot, centerRoot, centerToPalette))
        {
            return false;
        }
        for (int node = 0; node < sourceCount; ++node)
        {
            if (!ComposeBoneMatrices(centerToPalette, untouchedSource[node], g_fpPaletteScratch[node]) ||
                !ReachBoneMatrixFinite(g_fpPaletteScratch[node]))
            {
                return false;
            }
        }
        replacement = g_fpPaletteScratch;
        return true;
    }

    void ReachProcessFpPalette(uint16_t tag, const BoneMatrix* root, BoneMatrix* destination, uintptr_t unused,
        const BoneMatrix* source, const int32_t* boneMap)
    {
        ReachFpPaletteFn original = g_reachOrigFpPalette;
        (void)LegacyObserveWeaponRenderModel(
            GameTitle::HaloReach, tag, g_reachCamera.generation.load(std::memory_order_acquire), source, boneMap);
        // One relaxed increment when a seat is occupied. Reach builds
        // the occupant's first-person arms and weapon through this exact
        // transaction, so a non-zero count while seated means the engine did
        // build them and any remaining fault is ours (placement or collapse);
        // a zero count means it never built them at all.
        if (g_reachVehicleTrimSnapshot.load(std::memory_order_relaxed) != 0)
        {
            g_reachSeatedFpPaletteCalls.fetch_add(1, std::memory_order_relaxed);
        }
        if (g_reachNestedOuterSuppressed)
        {
            if (original)
                original(tag, root, destination, unused, source, boneMap);
            return;
        }

        // Match and consume the newest outstanding interpolation transaction by
        // exact source pointer. This is the same final-palette ownership model
        // used by Halo 3 and ODST; no render-model palette is admitted by guess.
        ReachFpInterpolationContext* matched = nullptr;
        for (auto& candidate : g_reachFpInterpolations)
        {
            const bool current = candidate.valid && source && candidate.source == source && g_reachFpPairScope.armed &&
                                 candidate.generation == g_reachCamera.generation &&
                                 candidate.generation == g_reachFpPairScope.generation &&
                                 candidate.preparedSerial == g_reachFpPairScope.preparedSerial;
            if (current && (!matched || candidate.captureSerial > matched->captureSerial))
                matched = &candidate;
        }
        ReachFpInterpolationContext context{};
        const bool contextCurrent = matched != nullptr;
        if (matched)
        {
            context = *matched;
            matched->valid = false;
        }
        const BoneMatrix* selectedSource = source;

        int paletteCount = 0;
        int32_t mapCopy[64]{};
        ReachFpBodyLayout observed{};
        bool bodyLayout = false;
        const bool paletteCountResolved = contextCurrent && ReachResolvePaletteNodeCount(tag, paletteCount);
        if (paletteCountResolved)
            ReachObserveFpLegPalette(tag, paletteCount);
        if (paletteCountResolved && boneMap &&
            (paletteCount == static_cast<int>(kReachSpartanFpBodyNodeCount) ||
                paletteCount == static_cast<int>(kReachEliteFpBodyNodeCount)) &&
            SafeReadBytes(boneMap, mapCopy, static_cast<size_t>(paletteCount) * sizeof(int32_t)))
        {
            bodyLayout = ResolveReachFpBodyLayout(std::span<const int32_t>{mapCopy, static_cast<size_t>(paletteCount)},
                static_cast<size_t>(context.liveSourceCount), observed);
            if (!bodyLayout) // a 47/41-node rig with an unpinned bone map
                g_unmappedFpRigs.Note(fp_rig_coverage::PackRig(
                    GameTitle::HaloReach, paletteCount, fp_rig_coverage::BoneMapFingerprint(mapCopy, paletteCount)));
        }
        else if (paletteCountResolved && paletteCount > 0 && paletteCount != kReachSpartanFpLegNodeCount &&
                 paletteCount != kReachEliteFpLegNodeCount)
            g_unmappedFpRigs.Note(fp_rig_coverage::PackRig(GameTitle::HaloReach, paletteCount, tag));
        const bool frozenLayoutValid = context.layout.Valid();
        const bool exactBodyMatchesFrozen =
            bodyLayout && frozenLayoutValid && tag == context.bodyTag && ReachLayoutsEqual(context.layout, observed);
        const ReachFpPaletteAction action = DecideReachFpPaletteAction(contextCurrent, frozenLayoutValid,
            context.transformed, bodyLayout, exactBodyMatchesFrozen, tag == context.bodyTag);
        const ReachFpLegPaletteKind frozenLegKind =
            g_reachFpPairScope.legPaletteValid ? g_reachFpPairScope.legPaletteKind : ReachFpLegPaletteKind::None;
        const bool exactSeatedLegPalette = paletteCountResolved &&
                                           ReachFpLegNodeCountMatchesKind(frozenLegKind, paletteCount) &&
                                           !ReachFpShouldCollapseVisiblePalette(g_reachFpPairScope.showSeatedLegs,
                                               frozenLegKind, g_reachFpPairScope.legPaletteTag, tag);
        auto restoreStockAndInvalidate = [&](bool relearnExactBody)
        {
            if (context.transformed)
            {
                selectedSource = context.untouchedLive;
                ReachRestoreFpLiveGraph(context);
            }
            ReachInvalidateLayoutNextPair(context);
            if (relearnExactBody)
                ReachLearnLayout(context, tag, observed);
        };

        if (action == ReachFpPaletteAction::LearnStockOnly)
        {
            ReachLearnLayout(context, tag, observed);
        }
        else if (action == ReachFpPaletteAction::RestoreStockAndInvalidate)
        {
            restoreStockAndInvalidate(bodyLayout);
        }
        else if (action == ReachFpPaletteAction::ArticulateKnownTransaction)
        {
            if (exactSeatedLegPalette)
            {
                selectedSource = context.untouchedLive;
                BoneMatrix centerRoot = context.targets.centerRoot;
                if (root)
                    centerRoot.scale = root->scale;
                const bool nativeBodyReady = root && context.targets.centerRootValid &&
                                             ReachBuildCenteredNativeFpPalette(*root, centerRoot, context.untouchedLive,
                                                 context.liveSourceCount, selectedSource);
                if (!nativeBodyReady)
                {
                    // This is a leg-presentation failure, not evidence that the
                    // proven 47/41 arms layout changed. Pass this exact body
                    // palette its untouched stock source and leave the
                    // controller-owned live marker graph alone. Invalidating
                    // the shared layout here would take working floating hands
                    // down with an optional native-body conversion failure.
                    selectedSource = context.untouchedLive;
                    g_reachFpLegPaletteBuildFailures.fetch_add(1, std::memory_order_relaxed);
                }
                else
                {
                    g_reachFpLegPalettePreserved.fetch_add(1, std::memory_order_relaxed);
                    PublishReachFpStatus(
                        2, static_cast<int>(context.layout.paletteBodyNodeCount), context.liveSourceCount);
                }
            }
            else if (!root || !context.targets.rightWristValid || context.liveSourceCount <= 0 ||
                     context.liveSourceCount > static_cast<int>(kReachFpMaxSourceNodeCount) ||
                     !ReachBoneMatrixFinite(*root))
            {
                restoreStockAndInvalidate(false);
            }
            else
            {
                FpInterpolationContext fp{};
                fp.source = source;
                fp.count = context.liveSourceCount;
                fp.player = context.interpolationView;
                fp.slot = context.interpolationSlot;
                fp.wrist = context.layout.rightWristSource;
                fp.cameraControl = context.layout.cameraControlSource;
                fp.elbow = context.layout.rightElbowSource;
                fp.shoulder = context.layout.rightShoulderSource;
                fp.wristDescendants = context.layout.rightHandSourceDescendants;
                fp.heldObjectStart = static_cast<int>(context.layout.paletteBodyNodeCount);
                fp.lWrist = context.layout.leftWristSource;
                fp.lElbow = context.layout.leftElbowSource;
                fp.lShoulder = context.layout.leftShoulderSource;
                fp.lWristDescendants = context.layout.leftHandSourceDescendants;
                fp.valid = true;
                selectedSource = context.untouchedLive;
                const BoneMatrix* replacement = selectedSource;
                FpExplicitPoseTargets targets = context.targets;
                targets.centerRoot.scale = root->scale;
                BoneMatrix alignedRight{};
                if (!ReachAlignRightTargetToAuthoredBarrel(
                        targets.rightWrist, context.untouchedLive[context.layout.rightWristSource], alignedRight))
                {
                    ReachMuzzleTrace("align-fail", tag, uint32_t(paletteCount), uint32_t(context.liveSourceCount), 0);
                    restoreStockAndInvalidate(false);
                    if (original)
                        original(tag, root, destination, unused, selectedSource, boneMap);
                    return;
                }
                targets.rightWrist = alignedRight;
                // Publish the pair the effect system needs. `stock` is where
                // the engine still believes the weapon is - the player's body -
                // and `alignedRight` is where the player actually sees it. The
                // muzzle flash resolves against the former; re-parenting it
                // between the two puts it on the gun with no offset.
                // MUST be centerRoot, not untouchedLive[rightWristSource].
                // The live-graph bone is in the first-person skeleton's LOCAL
                // space; effect matrices are in world space. Comparing them put
                // the nearest approach at 75,842 mm (in a probe build) - not a
                // near miss, two different coordinate systems. centerRoot and
                // alignedRight are both absolute world poses supplied by the
                // Reach adapter, so distances between them and an effect matrix
                // are meaningful. centerRoot sits at the player's head, which
                // is exactly where the stuck flash renders.
                if (context.targets.centerRootValid)
                {
                    ReachPublishWeaponAnchor(context.targets.centerRoot, alignedRight, g_reachCamera.generation);
                }
                if (ReachBoneMatrixFinite(alignedRight))
                {
                    g_reachWeaponAnchorMoved = alignedRight;
                    g_reachWeaponAnchorPending = true;
                }
                bool armsSolved = false;
                const bool reconstructed = ReconstructVisiblePaletteSource(
                    tag, fp, *root, source, replacement, &targets, context.untouchedLive, &armsSolved);
                selectedSource = replacement;
                bool leftHandBound = reconstructed && selectedSource == g_fpPaletteScratch;
                // Persistent support grip: for a RESOLVED pair the
                // visible support attachment follows the frozen relationship
                // trust, never the aim-authority flag: a trusted same-owner
                // invocation keeps the support hand attached even when aim
                // authority is off, and an owner-untrusted invocation never
                // inherits it. With the feature off the pair is unresolved and
                // `twoHandAimActive` remains the only input (PG-off parity).
                const bool supportGripAttached =
                    targets.supportGripResolved ? targets.supportGripAttached : targets.twoHandAimActive;
                if (leftHandBound && ReachShouldBindVisibleLeftHandToController(supportGripAttached))
                {
                    leftHandBound = ReachBindFloatingLeftHandToController(
                        *root, fp, context.layout.leftControllerOwnedSourceBranch, targets);
                }
                bool anatomicalLeftHanded = false;
                if (reconstructed && leftHandBound && targets.handAlignment)
                {
                    anatomicalLeftHanded = ReachRouteLeftHandedPalette(tag, boneMap, fp, *root, context.targets);
                    (anatomicalLeftHanded ? g_reachAnatomicalApplications : g_reachAnatomicalRefusals)
                        .fetch_add(1, std::memory_order_relaxed);
                }
                bool outputFinite = reconstructed && leftHandBound;
                for (int i = 0; outputFinite && i < fp.count; ++i)
                    outputFinite = ReachBoneMatrixFinite(g_fpPaletteScratch[i]);
                if (!outputFinite)
                {
                    restoreStockAndInvalidate(false);
                }
                else
                {
                    // Contact buffers name controller roles. Source masks name
                    // anatomy, so publish the same assignment as the palette.
                    FpInterpolationContext contactContext = fp;
                    if (anatomicalLeftHanded)
                    {
                        std::swap(contactContext.wrist, contactContext.lWrist);
                        std::swap(contactContext.wristDescendants, contactContext.lWristDescendants);
                    }
                    LegacyPublishReloadTarget(GameTitle::HaloReach, tag,
                        g_reachCamera.generation.load(std::memory_order_acquire), contactContext, *root,
                        g_fpPaletteScratch, boneMap, &targets);
                    LegacyPublishWorldCollisionVolumes(GameTitle::HaloReach, tag, contactContext, *root,
                        g_fpPaletteScratch, LegacyCollisionIgnoredObject(GameTitle::HaloReach),
                        g_reachCamera.generation.load(std::memory_order_acquire), &targets);
                    // The exact hand/arm masks and held-object boundary come
                    // from the pinned HREK/retail layouts. When visible arms
                    // are requested, ReconstructVisiblePaletteSource has
                    // already solved the verified shoulder/elbow/wrist chains
                    // against this pair's controller targets. Keep unrelated
                    // body nodes hidden here; body_wip owns native body
                    // visibility separately.
                    if (selectedSource == g_fpPaletteScratch)
                    {
                        const uint64_t keep = fp.wristDescendants | fp.lWristDescendants;
                        const bool showTrackedArms =
                            ReachMayShowControllerTrackedArms(!g_config.floating_hands, g_config.arm_ik,
                                supportGripAttached, targets.rightWristValid, targets.leftWristValid, armsSolved);
                        const uint64_t hiddenLeft =
                            context.layout.leftControllerOwnedSourceBranch & ~fp.lWristDescendants;
                        const uint64_t hiddenRight =
                            context.layout.rightControllerOwnedSourceBranch & ~fp.wristDescendants;
                        BoneMatrix collapsedAtLeftWrist = g_fpPaletteScratch[fp.lWrist];
                        collapsedAtLeftWrist.scale = 0.0001f;
                        BoneMatrix collapsedAtRightWrist = g_fpPaletteScratch[fp.wrist];
                        collapsedAtRightWrist.scale = 0.0001f;
                        for (int i = 0; i < fp.count; ++i)
                        {
                            const bool hand = i < 64 && (keep & (uint64_t{1} << i));
                            const bool held = fp.heldObjectStart >= 0 && i >= fp.heldObjectStart;
                            if (!hand && !held)
                            {
                                const bool leftArm = i < 64 && (hiddenLeft & (uint64_t{1} << i));
                                const bool rightArm = i < 64 && (hiddenRight & (uint64_t{1} << i));
                                const auto paletteAction =
                                    ReachFpPaletteVisibility(hand, held, showTrackedArms, leftArm, rightArm);
                                if (paletteAction == ReachFpVisibilityAction::Keep)
                                    continue;
                                if (paletteAction == ReachFpVisibilityAction::CollapseAtLeftWrist)
                                {
                                    g_fpPaletteScratch[i] = collapsedAtLeftWrist;
                                }
                                else if (paletteAction == ReachFpVisibilityAction::CollapseAtRightWrist)
                                {
                                    g_fpPaletteScratch[i] = collapsedAtRightWrist;
                                }
                                else
                                {
                                    g_fpPaletteScratch[i].scale = 0.0001f;
                                }
                            }
                        }
                    }
                    PublishReachFpStatus(
                        2, static_cast<int>(context.layout.paletteBodyNodeCount), context.liveSourceCount);
                }
            }
        }
        if (original)
            original(tag, root, destination, unused, selectedSource, boneMap);

        if (original && selectedSource == g_fpPaletteScratch && !bodyLayout &&
            action == ReachFpPaletteAction::ArticulateKnownTransaction && !g_reachNestedOuterSuppressed)
            ReachPublishMuzzlePalette(tag, context, destination);
        else if (contextCurrent &&
                 !bodyLayout) // why the builder was not called for this palette (diagnostic, once per distinct state)
            ReachMuzzleTrace("skip", tag, uint32_t(action),
                (original ? 1u : 0u) | (selectedSource == g_fpPaletteScratch ? 2u : 0u) |
                    (g_reachNestedOuterSuppressed ? 4u : 0u) | (context.transformed ? 8u : 0u) |
                    (context.targets.rightWristValid ? 16u : 0u) | (frozenLayoutValid ? 32u : 0u),
                uint32_t(paletteCount));

        if (original && selectedSource == g_fpPaletteScratch && exactBodyMatchesFrozen &&
            action == ReachFpPaletteAction::ArticulateKnownTransaction)
            ReachApplyVisualHandOffsets(tag, boneMap, context, destination);

        if (original && selectedSource == g_fpPaletteScratch && !bodyLayout &&
            action == ReachFpPaletteAction::ArticulateKnownTransaction && !g_reachNestedOuterSuppressed)
            ReachApplyFreeSupportFingerPose(tag, paletteCount, context, destination);

        // An earlier candidate put alignedRight here after the visible palette had
        // already been composed. alignedRight is an absolute world pose; the
        // live interpolation records are local to the first-person root. The
        // same source file had already measured those as different coordinate
        // spaces, but the candidate nevertheless copied the world matrix into
        // one local record. It was ineffective for the muzzle flash and was
        // superseded by the accepted loaded-tag retarget, yet the bad
        // write remained active. Do not let a later render consumer observe
        // that internally inconsistent skeleton. The coherent rigid local
        // graph transform performed before the palette remains unchanged.
        if (g_reachWeaponAnchorPending && context.layout.rightWristSource >= 0 &&
            context.layout.rightWristSource < context.liveSourceCount && context.source)
        {
            g_reachWorldWristWritesPrevented.fetch_add(1, std::memory_order_relaxed);
        }
        g_reachWeaponAnchorPending = false;
    }
    __declspec(noinline) void __fastcall ReachFpPalette(uint16_t tag, const BoneMatrix* root, BoneMatrix* destination,
        uintptr_t unused, const BoneMatrix* source, const int32_t* boneMap)
    {
        g_reachCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            ReachProcessFpPalette(tag, root, destination, unused, source, boneMap);
        }
        __finally
        {
            g_reachCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }
} // namespace
namespace reach_bridge
{
    unsigned char* ReachVehicleObjectData(int32_t handle, uint8_t& kind)
    {
        kind = 0;
        if (handle == -1)
            return nullptr;
        const int16_t wantedSalt = static_cast<int16_t>(handle >> 16);
        if (!wantedSalt)
            return nullptr;
        auto** slots = reinterpret_cast<void**>(__readgsqword(0x58));
        const auto* tlsIndex = reinterpret_cast<const uint32_t*>(g_reachCamera.base + kReachEngineTlsIndexRva);
        if (!slots || !tlsIndex || *tlsIndex >= 0x200)
            return nullptr;
        auto* tls = reinterpret_cast<unsigned char*>(slots[*tlsIndex]);
        auto* collection = tls ? *reinterpret_cast<unsigned char**>(tls + kReachTlsObjectCollectionOffset) : nullptr;
        if (!collection || !*(collection + kReachDataCollectionInitializedOffset) ||
            *reinterpret_cast<const uint64_t*>(collection + kReachDataCollectionStrideOffset) !=
                kReachObjectEntryStride)
        {
            return nullptr;
        }
        const uint32_t index = static_cast<uint16_t>(handle);
        const int32_t count = *reinterpret_cast<const int32_t*>(collection + kReachDataCollectionCountOffset);
        auto* entries = *reinterpret_cast<unsigned char**>(collection + kReachDataCollectionEntriesOffset);
        if (!entries || count <= 0 || count > 0x10000 || index >= static_cast<uint32_t>(count))
        {
            return nullptr;
        }
        auto* entry = entries + static_cast<size_t>(index) * kReachObjectEntryStride;
        const int16_t liveSalt = *reinterpret_cast<const int16_t*>(entry + kReachObjectEntrySaltOffset);
        if (!liveSalt || liveSalt != wantedSalt)
            return nullptr;
        kind = *(entry + kReachObjectEntryKindOffset);
        return *reinterpret_cast<unsigned char**>(entry + kReachObjectEntryDataOffset);
    }
} // namespace reach_bridge
namespace
{
#include "reach_contact_melee_runtime.inl"
#include "reach_muzzle_ownership.inl"
#include "reach_muzzle_publication.inl"
#include "reach_muzzle_shots.inl"
#include "reach_muzzle_lifecycle.inl"
#include "reach_weapon_haptics.inl"
#include "reach_grab_pickup.inl"
#include "reach_visual_hand_offset.inl"
    // Optional per-frame read of the SAME local unit handle whose native
    // camera-info call proved the occupied seat. Keep its SEH boundary separate
    // from the vehicle-frame transaction: an unavailable aim sample falls back
    // to the compact camera for this frame and never disables the camera core.
    bool ReachReadUnitAimingVector(int32_t unitHandle, float out[3])
    {
        if (!out || unitHandle == -1)
            return false;
        float raw[3]{};
        bool copied = false;
        __try
        {
            uint8_t unitKind = 0;
            const unsigned char* unitData = ReachVehicleObjectData(unitHandle, unitKind);
            if (unitData && unitKind != kReachObjectKindVehicle)
            {
                memcpy(raw, unitData + kReachUnitAimingVectorOffset, sizeof(raw));
                copied = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading the Reach unit aiming vector faulted");
            copied = false;
        }
        return copied && ReachNormalizeUnitAimingVector(raw, out);
    }

    unsigned char* ReachPackedTagBlockElement(uint32_t packedData, int index, size_t stride)
    {
        if (index < 0 || stride == 0 || (stride & 3u) != 0)
            return nullptr;
        const uint32_t segment = packedData >> 28;
        if (segment >= 16 || !g_reachCamera.base)
            return nullptr;
        auto* segmentBase = *reinterpret_cast<unsigned char**>(
            g_reachCamera.base + kReachNodeRecordBlockTableRva + static_cast<size_t>(segment) * sizeof(void*));
        if (!segmentBase)
            return nullptr;
        const uint64_t word = static_cast<uint64_t>(packedData) + static_cast<uint64_t>(index) * (stride / 4u);
        if (word > 0xFFFFFFFFull)
            return nullptr;
        return segmentBase + static_cast<size_t>(word) * 4u;
    }

    ReachVehicleId ReachResolveVehicleDefinition(const unsigned char* definition, uint32_t definitionDatum,
        ReachVehicleFingerprint* outFingerprint = nullptr, int32_t* outObservedType = nullptr)
    {
        if (!definition || !g_reachCamera.vehicleType)
            return ReachVehicleId::Unknown;
        ReachVehicleFingerprint fingerprint{};
        memcpy(&fingerprint.radius, definition + kReachVehicleBoundsRadiusOffset, sizeof(fingerprint.radius));
        memcpy(&fingerprint.offsetX, definition + kReachVehicleBoundsOffsetX, sizeof(fingerprint.offsetX));
        memcpy(&fingerprint.offsetY, definition + kReachVehicleBoundsOffsetY, sizeof(fingerprint.offsetY));
        memcpy(&fingerprint.offsetZ, definition + kReachVehicleBoundsOffsetZ, sizeof(fingerprint.offsetZ));
        const int32_t observedType = g_reachCamera.vehicleType(definitionDatum);
        if (outFingerprint)
            *outFingerprint = fingerprint;
        if (outObservedType)
            *outObservedType = observedType;
        const ReachVehicleId identity = ReachResolveVehicleFingerprint(fingerprint);
        if (identity == ReachVehicleId::Unknown || !ReachVehiclePhysicsTypeMatches(identity, fingerprint, observedType))
        {
            return ReachVehicleId::Unknown;
        }
        return identity;
    }

    ReachSeatLeaseState ReachNativeSeatLeaseState()
    {
        return static_cast<ReachSeatLeaseState>(g_reachVehicleSeatPatchState.load(std::memory_order_acquire));
    }

    const char* ReachNativeSeatLeaseStateName(ReachSeatLeaseState state)
    {
        switch (state)
        {
        case ReachSeatLeaseState::Empty:
            return "empty";
        case ReachSeatLeaseState::Active:
            return "active";
        case ReachSeatLeaseState::RestorePending:
            return "restore-pending";
        case ReachSeatLeaseState::External:
            return "external/native";
        case ReachSeatLeaseState::Installing:
            return "installing";
        case ReachSeatLeaseState::CleanupLocked:
            return "cleanup-locked";
        default:
            return "invalid";
        }
    }

    bool ReachAcquireNativeSeatLeaseCleanupLock(ReachSeatLeaseState& observed)
    {
        observed = ReachNativeSeatLeaseState();
        for (;;)
        {
            if (ReachSeatLeaseCleanupLocked(observed))
                return true;
            if (!ReachSeatLeaseCanAcquireCleanupLock(observed))
                return false;
            uint32_t expected = static_cast<uint32_t>(observed);
            if (g_reachVehicleSeatPatchState.compare_exchange_strong(expected,
                    static_cast<uint32_t>(ReachSeatLeaseState::CleanupLocked), std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                observed = ReachSeatLeaseState::CleanupLocked;
                g_reachVehicleSeatPatchSerial.fetch_add(1, std::memory_order_release);
                return true;
            }
            observed = static_cast<ReachSeatLeaseState>(expected);
        }
    }

    void ReachPublishNativeSeatLeaseState(ReachSeatLeaseState state, bool failure)
    {
        const uint32_t next = static_cast<uint32_t>(state);
        const uint32_t previous = g_reachVehicleSeatPatchState.exchange(next, std::memory_order_acq_rel);
        if (failure)
        {
            g_reachVehicleSeatPatchFailures.fetch_add(1, std::memory_order_relaxed);
        }
        if (previous != next || failure)
        {
            g_reachVehicleSeatPatchSerial.fetch_add(1, std::memory_order_release);
        }
    }

    uint32_t* ReachResolveLiveSeatFlagsPointer(const ReachSeatLeaseKey& key, bool requireOccupiedSeat)
    {
        if (!ReachSeatLeaseKeyValid(key) ||
            key.generation != g_reachCamera.generation.load(std::memory_order_acquire) || !g_reachCamera.tagGet)
        {
            return nullptr;
        }

        // Both object handles retain their salts. The occupant may have exited
        // or changed seats before restoration, so the old unit must still be a
        // live exact object but need not still point at the old seat.
        uint8_t unitKind = 0;
        if (!ReachVehicleObjectData(key.unitHandle, unitKind) || unitKind == kReachObjectKindVehicle)
        {
            return nullptr;
        }

        ReachNativeUnitCameraInfo info{};
        if (requireOccupiedSeat)
        {
            if (!g_reachCamera.vehiclePlayerUnitByOutputUser || !g_reachCamera.unitGetCameraInfo ||
                g_reachCamera.vehiclePlayerUnitByOutputUser(0) != key.unitHandle)
            {
                return nullptr;
            }
            g_reachCamera.unitGetCameraInfo(key.unitHandle, &info);
            if (info.directParent != key.directParent || info.seatIndex != key.seatIndex || !info.seatCamera)
            {
                return nullptr;
            }
        }

        uint8_t parentKind = 0;
        unsigned char* parentData = ReachVehicleObjectData(key.directParent, parentKind);
        if (!parentData || parentKind != kReachObjectKindVehicle ||
            *reinterpret_cast<const uint32_t*>(parentData + kReachObjectDefinitionDatumOffset) != key.definitionDatum)
        {
            return nullptr;
        }
        auto* definition =
            static_cast<unsigned char*>(g_reachCamera.tagGet(kReachVehicleGroupTag, key.definitionDatum));
        if (!definition)
            return nullptr;
        const int32_t seatCount = *reinterpret_cast<const int32_t*>(definition + kReachVehicleSeatsBlockOffset);
        const uint32_t packedSeats = *reinterpret_cast<const uint32_t*>(definition + kReachVehicleSeatsBlockOffset + 4);
        if (seatCount <= 0 || seatCount > kReachVehicleSeatLimit || key.seatIndex >= seatCount)
        {
            return nullptr;
        }
        unsigned char* seat = ReachPackedTagBlockElement(packedSeats, key.seatIndex, kReachVehicleSeatStride);
        if (!seat || (requireOccupiedSeat && seat + kReachSeatCameraOffset != info.seatCamera))
            return nullptr;
        return reinterpret_cast<uint32_t*>(seat + kReachSeatFlagsOffset);
    }

    // HREK following_camera.cpp names unit-camera bit 2 "hides player-unit
    // from camera": only the player controlling this camera stops seeing its
    // world unit. The pinned retail following-camera consumer resolves this
    // exact occupied seat camera and tests byte mask 0x04 at +0x1DD3B2.
    // Keep the pointer on this render stack only; loaded tag storage is never
    // retained across an outer call or published to another thread.
    struct ReachUnitCameraHideScope
    {
        volatile SHORT* flags = nullptr;
        uint16_t originalFlags = 0;
        uint16_t writtenFlags = 0;
        bool ownsMutation = false;
    };

    uint16_t* ReachResolveLiveUnitCameraFlagsPointer(const ReachSeatLeaseKey& key)
    {
        uint32_t* seatFlags = ReachResolveLiveSeatFlagsPointer(key, true);
        if (!seatFlags)
            return nullptr;
        auto* seat = reinterpret_cast<unsigned char*>(seatFlags) - kReachSeatFlagsOffset;
        return reinterpret_cast<uint16_t*>(seat + kReachSeatCameraOffset + kReachUnitCameraFlagsOffset);
    }

    bool ReachBeginUnitCameraPlayerHide(const ReachVehicleFrame& frame, ReachUnitCameraHideScope& scope)
    {
        scope = {};
        if (!frame.active || !g_config.vehicle_first_person || !g_config.vehicle_hide_body ||
            g_reachCamera.teardownRequested.load(std::memory_order_acquire) ||
            g_reachCamera.vehicleCameraBindingState.load(std::memory_order_acquire) !=
                static_cast<uint8_t>(ReachVehicleCameraBindingState::Installed))
        {
            return false;
        }
        const ReachSeatLeaseKey key{g_reachCamera.generation.load(std::memory_order_acquire), frame.unitHandle,
            frame.directParent, frame.definitionDatum, frame.seatIndex};
        if (!ReachSeatLeaseKeyValid(key))
            return false;

        bool admitted = false;
        bool failed = false;
        __try
        {
            uint16_t* liveFlags = ReachResolveLiveUnitCameraFlagsPointer(key);
            if (!liveFlags)
            {
                failed = true;
                __leave;
            }
            auto* flags = reinterpret_cast<volatile SHORT*>(liveFlags);
            const uint16_t original = static_cast<uint16_t>(InterlockedCompareExchange16(flags, 0, 0));
            const uint16_t written = ReachUnitCameraHidePlayerFlags(original);
            if (written == original)
            {
                g_reachUnitCameraHideAlreadySet.fetch_add(1, std::memory_order_relaxed);
                admitted = true;
                __leave;
            }
            const uint16_t prior = static_cast<uint16_t>(
                InterlockedCompareExchange16(flags, static_cast<SHORT>(written), static_cast<SHORT>(original)));
            if (prior != original)
            {
                failed = true;
                __leave;
            }
            scope.flags = flags;
            scope.originalFlags = original;
            scope.writtenFlags = written;
            scope.ownsMutation = true;
            g_reachUnitCameraHideSets.fetch_add(1, std::memory_order_relaxed);
            admitted = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("hiding the Reach player in the unit camera faulted");
            failed = true;
        }
        if (failed)
        {
            g_reachUnitCameraHideFailures.fetch_add(1, std::memory_order_relaxed);
        }
        return admitted;
    }

    void ReachEndUnitCameraPlayerHide(ReachUnitCameraHideScope& scope)
    {
        if (!scope.ownsMutation || !scope.flags)
            return;
        bool restored = false;
        __try
        {
            // This scope owns only bit 2. Preserve any unrelated flag change
            // made during the stock render while always returning our bit to
            // its entry value. Loaded tag words are normally immutable here,
            // but the bounded CAS loop also isolates another runtime writer.
            for (int attempt = 0; attempt < 8; ++attempt)
            {
                const uint16_t current = static_cast<uint16_t>(InterlockedCompareExchange16(scope.flags, 0, 0));
                const uint16_t desired = ReachUnitCameraRestorePlayerFlags(current, scope.originalFlags);
                if (current == desired)
                {
                    restored = true;
                    break;
                }
                const uint16_t prior = static_cast<uint16_t>(InterlockedCompareExchange16(
                    scope.flags, static_cast<SHORT>(desired), static_cast<SHORT>(current)));
                if (prior == current)
                {
                    restored = true;
                    break;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("restoring the Reach unit camera flags faulted");
            restored = false;
        }
        if (restored)
        {
            g_reachUnitCameraHideRestores.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
            // Never overwrite a third value: a title/tag writer wins, and this
            // optional feature alone degrades for the frame.
            g_reachUnitCameraHideFailures.fetch_add(1, std::memory_order_relaxed);
        }
        scope = {};
    }

    bool ReachRestoreNativeSeatLease()
    {
        if (!kReachSeatFirstPersonPresentationEnabled)
            return true;
        const ReachSeatLeaseState state = ReachNativeSeatLeaseState();
        if (!ReachSeatLeaseOwnsMutation(state))
            return state != ReachSeatLeaseState::Installing;

        const ReachSeatLeasePayload lease = g_reachNativeSeatLease;
        bool restored = false;
        bool external = false;
        __try
        {
            uint32_t* liveFlags = ReachResolveLiveSeatFlagsPointer(lease.key, false);
            if (!liveFlags)
                __leave;
            auto* flags = reinterpret_cast<volatile LONG*>(liveFlags);
            const uint32_t current = static_cast<uint32_t>(InterlockedCompareExchange(flags, 0, 0));
            const ReachSeatLeaseRestoreDisposition disposition =
                ReachClassifySeatLeaseRestore(current, lease.originalFlags, lease.writtenFlags);
            if (disposition == ReachSeatLeaseRestoreDisposition::AlreadyOriginal)
            {
                restored = true;
                __leave;
            }
            if (disposition == ReachSeatLeaseRestoreDisposition::ExternalWrite)
            {
                external = true;
                __leave;
            }
            const uint32_t prior = static_cast<uint32_t>(InterlockedCompareExchange(
                flags, static_cast<LONG>(lease.originalFlags), static_cast<LONG>(lease.writtenFlags)));
            const ReachSeatLeaseRestoreDisposition afterCas =
                ReachClassifySeatLeaseRestore(prior, lease.originalFlags, lease.writtenFlags);
            restored = afterCas == ReachSeatLeaseRestoreDisposition::RestoreOriginal ||
                       afterCas == ReachSeatLeaseRestoreDisposition::AlreadyOriginal;
            external = !restored;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("restoring the Reach seat lease faulted");
            restored = false;
            external = false;
        }

        if (restored)
        {
            g_reachNativeSeatLease = {};
            ReachPublishNativeSeatLeaseState(ReachSeatLeaseState::Empty, false);
            return true;
        }
        if (external)
        {
            // The word is neither the exact value we wrote nor its exact
            // original. The engine/tag writer wins permanently for this key.
            ReachPublishNativeSeatLeaseState(ReachSeatLeaseState::External, true);
            return false;
        }
        ReachPublishNativeSeatLeaseState(
            ReachSeatLeaseState::RestorePending, state != ReachSeatLeaseState::RestorePending);
        return false;
    }

    // Returns true only when this call owns an actual bit-4 mutation. A seat
    // that is naturally first person is recorded as External/native and is
    // deliberately never restored or rewritten.
    //
    // This is Halo 3's accepted seat-flag lifetime. It clears
    // the occupied seat's `third person camera` bit ONCE on entry and leaves it
    // clear for the whole occupation (Halo3EnsureFirstPersonSeatFlag returns
    // early on `sameSeat && patch.active`), restoring only on seat change,
    // exit, config disable or teardown. An earlier design instead restored the bit at the
    // top of every outer render callback and re-cleared it before
    // main_render_view, so the word toggled twice per frame - that is the
    // "vehicle shaking" a headset run rejected, and a bit that is
    // clear only for part of a tick cannot hold the director in first-person
    // perspective long enough for the engine to build the first-person weapon
    // models. The value-only key and re-resolve-before-restore safety of that
    // design is kept; only the toggling is gone.
    bool ReachEnsureNativeFirstPersonSeat(const ReachVehicleFrame& frame)
    {
        if (!kReachSeatFirstPersonPresentationEnabled)
            return false;
        // Halo 3's own first act: turning the feature off, or losing the
        // binding, hands the seat's word back immediately rather than waiting
        // for the player to climb out.
        // Only a seat that authors HREK bit 5 `allows weapons` - i.e. a
        // passenger holding their own gun - takes this. Halo 3's seat-flag result is
        // specifically about "the first-person weapon in `allows weapons`
        // passenger seats"; a driver, mounted gunner or walk-up turret has no
        // personal weapon for the engine to bring up, so clearing their seat's
        // camera bit buys nothing and a headset run reported it
        // breaking every turret. Those seats keep exactly the behavior headset
        // testing called "a better state" one build earlier.
        if (!frame.active || !g_config.vehicle_first_person || (frame.seatFlags & kReachSeatAllowsWeaponsBit) == 0 ||
            g_reachCamera.teardownRequested.load(std::memory_order_acquire) ||
            g_reachCamera.vehicleCameraBindingState.load(std::memory_order_acquire) !=
                static_cast<uint8_t>(ReachVehicleCameraBindingState::Installed))
        {
            ReachRestoreNativeSeatLease();
            return false;
        }
        const ReachSeatLeaseKey key{g_reachCamera.generation.load(std::memory_order_acquire), frame.unitHandle,
            frame.directParent, frame.definitionDatum, frame.seatIndex};
        if (!ReachSeatLeaseKeyValid(key))
            return false;

        ReachSeatLeaseState state = ReachNativeSeatLeaseState();
        // Halo 3's `sameSeat && patch.active` early-out: the seat we already
        // own stays owned and is never rewritten frame to frame.
        if (ReachSeatLeaseOwnsMutation(state) && ReachSeatLeaseKeyEqual(g_reachNativeSeatLease.key, key))
        {
            return state == ReachSeatLeaseState::Active;
        }
        // A different seat, vehicle or generation is a new occupation. Give the
        // old one its exact word back before claiming this one; a failed
        // restore leaves the old lease pending and blocks the new claim below.
        if (ReachSeatLeaseOwnsMutation(state))
        {
            ReachRestoreNativeSeatLease();
            state = ReachNativeSeatLeaseState();
        }
        if (ReachSeatLeaseBlocksKey(state, g_reachNativeSeatLease.key, key))
        {
            return false;
        }
        uint32_t expected = static_cast<uint32_t>(state);
        if (!g_reachVehicleSeatPatchState.compare_exchange_strong(expected,
                static_cast<uint32_t>(ReachSeatLeaseState::Installing), std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
            return false;
        }

        // Installing already won the conclusive same-atomic race against
        // CleanupLocked. The independent lifecycle flags are advisory here:
        // this recheck avoids an unnecessary write when their cleanup request
        // is visible, but teardown safety never depends on observing them.
        if (g_reachCamera.teardownRequested.load(std::memory_order_acquire) ||
            g_reachCamera.vehicleCameraBindingState.load(std::memory_order_acquire) !=
                static_cast<uint8_t>(ReachVehicleCameraBindingState::Installed))
        {
            g_reachNativeSeatLease = {};
            ReachPublishNativeSeatLeaseState(ReachSeatLeaseState::Empty, false);
            return false;
        }

        bool installed = false;
        bool natural = false;
        bool external = false;
        bool faulted = false;
        bool writeMayHaveOccurred = false;
        __try
        {
            uint32_t* liveFlags = ReachResolveLiveSeatFlagsPointer(key, true);
            if (!liveFlags)
            {
                faulted = true;
                __leave;
            }
            auto* flags = reinterpret_cast<volatile LONG*>(liveFlags);
            const uint32_t original = static_cast<uint32_t>(InterlockedCompareExchange(flags, 0, 0));
            const uint32_t written = original & ~kReachSeatThirdPersonCameraBit;
            g_reachNativeSeatLease = {key, original, written};
            if (original == written)
            {
                natural = true;
                __leave;
            }
            if (g_reachCamera.teardownRequested.load(std::memory_order_acquire))
            {
                __leave;
            }
            writeMayHaveOccurred = true;
            const uint32_t prior = static_cast<uint32_t>(
                InterlockedCompareExchange(flags, static_cast<LONG>(written), static_cast<LONG>(original)));
            writeMayHaveOccurred = false;
            if (prior != original)
            {
                external = true;
                __leave;
            }
            installed = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("setting the Reach first-person seat flag faulted");
            faulted = true;
        }

        if (writeMayHaveOccurred)
        {
            // An SEH fault left the CAS outcome unknowable. Retain the exact
            // key/words as owned and make the engine-thread restore path prove
            // the outcome; never discard a possibly live mutation.
            installed = true;
        }
        if (installed)
        {
            ReachPublishNativeSeatLeaseState(ReachSeatLeaseState::Active, faulted);
            if (g_reachCamera.teardownRequested.load(std::memory_order_acquire))
            {
                ReachRestoreNativeSeatLease();
                return false;
            }
            return true;
        }
        if (natural || external)
        {
            ReachPublishNativeSeatLeaseState(ReachSeatLeaseState::External, external);
            return false;
        }
        g_reachNativeSeatLease = {};
        ReachPublishNativeSeatLeaseState(ReachSeatLeaseState::Empty, faulted);
        return false;
    }

    bool ReadReachEnginePaused(bool& paused);

    // A native pause suspends, never exits, the occupation. While Reach is
    // paused its player-unit resolver reports no unit, so sampling published
    // Unknown, the debounce left Vehicle, the seat lease was restored and the
    // exact-seat latch cleared - and the resume then replayed a full seat
    // ENTRY (play-space recenter, seat camera-bit clear, LT/X rebind) about
    // 2 s later. Both captured Reach kick-to-menu events (07:21:14 and
    // 08:36:16, 2026-08-06) follow exactly that replayed entry by 1.5-3 s.
    // The seat-lease rule already holds the occupation across camera-only misses;
    // a native pause is the same suspension, so every published seat state
    // holds across it and nothing is restored or reapplied at resume.
    bool ReachVehicleSamplingSuspendedByPause()
    {
        bool paused = false;
        return ReadReachEnginePaused(paused) && paused;
    }

    // Sample only on Reach's proven normal-player outer-render thread, whose
    // engine TLS is valid. The XInput hook reads only the resulting lock-free
    // snapshot and never calls title code from MCC's input thread.
    void ReachSampleVehicleInputState(uint32_t windowIndex, uintptr_t returnAddress)
    {
        // The camera-info sampler below covers every seat, including Reach's
        // type-6 mounted turrets. When it is installed it is the sole publisher
        // so this older unit_in_vehicle predicate cannot briefly overwrite a
        // turret sample with OnFoot between two input polls.
        if (g_reachCamera.vehicleCameraBindingState.load(std::memory_order_acquire) ==
            static_cast<uint8_t>(ReachVehicleCameraBindingState::Installed))
        {
            return;
        }
        if (windowIndex != 0 ||
            g_reachCamera.vehicleInputBindingState.load(std::memory_order_acquire) !=
                static_cast<uint8_t>(ReachVehicleInputBindingState::Installed) ||
            g_reachCamera.teardownRequested.load(std::memory_order_acquire))
            return;

        const uintptr_t base = g_reachCamera.base;
        const uint32_t generation = g_reachCamera.generation.load(std::memory_order_acquire);
        if (!base || !generation ||
            ClassifyReachOuterRenderCaller(base, kReachRetailImageSize, returnAddress) !=
                ReachOuterRenderCaller::NormalPlayer)
            return;
        if (ReachVehicleSamplingSuspendedByPause())
            return; // hold the previous snapshot; a pause is not an exit

        ReachVehicleInputState state = ReachVehicleInputState::Unknown;
        bool faulted = false;
        __try
        {
            const int unit = g_reachCamera.playerUnitByOutputUser(0);
            if (unit != -1)
            {
                state = g_reachCamera.unitInVehicle(unit) ? ReachVehicleInputState::Vehicle
                                                          : ReachVehicleInputState::OnFoot;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("sampling the Reach vehicle input state faulted");
            faulted = true;
        }
        if (faulted)
        {
            // Disable only this optional refinement. The controller path falls
            // back to its prior Reach-wide swap; stereo ownership is untouched.
            g_reachVehicleInputSnapshot.store(0, std::memory_order_release);
            g_reachVehicleInputFaults.fetch_add(1, std::memory_order_relaxed);
            g_reachCamera.vehicleInputBindingState.store(
                static_cast<uint8_t>(ReachVehicleInputBindingState::StockFallback), std::memory_order_release);
            return;
        }
        g_reachVehicleInputSnapshot.store(ReachVehicleInputSnapshot(generation, state), std::memory_order_release);
    }

    void ReachUpdateVehicleTransition(Halo3VehicleState state, uint64_t nowMs)
    {
        g_reachVehicleSampleMs.store(nowMs, std::memory_order_relaxed);
        const Halo3VehicleState previousStable = g_reachVehicleFpDebounce.stable;
        if (!g_reachVehicleFpDebounce.Update(state, kHalo3VehicleDebounceFrames))
        {
            return;
        }
        g_reachVehicleFpStable.store(static_cast<uint32_t>(g_reachVehicleFpDebounce.stable), std::memory_order_relaxed);
        // Entry is a full exact-seat play-space reset performed only after the
        // immersive vehicle camera actually wins. A settled exit remains the
        // accepted position-only neutralization so leaving a car cannot snap
        // the player's facing.
        if (g_config.vehicle_first_person && previousStable == Halo3VehicleState::Vehicle &&
            g_reachVehicleFpDebounce.stable == Halo3VehicleState::OnFoot && g_config.vehicle_recenter_on_seat)
        {
            g_needPosRecenter.store(true, std::memory_order_release);
            g_reachVehicleExitRecenters.fetch_add(1, std::memory_order_relaxed);
        }
    }

    bool ReachSampleVehicleCameraFrame(ReachVehicleFrame& frame)
    {
        frame = {};
        if (g_reachCamera.vehicleCameraBindingState.load(std::memory_order_acquire) !=
                static_cast<uint8_t>(ReachVehicleCameraBindingState::Installed) ||
            g_reachCamera.teardownRequested.load(std::memory_order_acquire))
        {
            return false;
        }
        const uint32_t generation = g_reachCamera.generation.load(std::memory_order_acquire);
        if (!generation || !g_reachCamera.vehiclePlayerUnitByOutputUser || !g_reachCamera.unitGetCameraInfo ||
            !g_reachCamera.objectMarkers || !g_reachCamera.tagGet)
        {
            return false;
        }
        // See ReachVehicleSamplingSuspendedByPause: return before ANY publish,
        // restore or transition so the whole occupation (input snapshot,
        // debounced state, seat lease, trim/shot occupation, exact-seat
        // latch) holds across a native pause and the resume replays nothing.
        if (ReachVehicleSamplingSuspendedByPause())
            return false;

        const uint64_t nowMs = GetTickCount64();
        bool unitKnown = false;
        bool seatOccupationObserved = false;
        bool structurallySeated = false;
        bool complete = false;
        bool identityMiss = false;
        bool markerFailure = false;
        bool boundsFailure = false;
        bool bounceFallback = false;
        bool followFallback = false;
        bool anchorFallback = false;
        bool faulted = false;
        bool unitAimValid = false;
        float unitAimForward[3]{};
        __try
        {
            do
            {
                const int32_t unit = g_reachCamera.vehiclePlayerUnitByOutputUser(0);
                unitKnown = unit != -1;
                if (!unitKnown)
                    break;
                ReachNativeUnitCameraInfo info{};
                g_reachCamera.unitGetCameraInfo(unit, &info);
                seatOccupationObserved =
                    ReachSeatOccupationObserved(info.directParent, info.seatIndex, kReachVehicleSeatLimit);
                structurallySeated = seatOccupationObserved && info.seatCamera != nullptr;
                if (!structurallySeated)
                    break;
                for (float component : info.worldCameraPoint)
                {
                    if (!std::isfinite(component))
                    {
                        structurallySeated = false;
                        break;
                    }
                }
                if (!structurallySeated)
                    break;

                unitAimValid = ReachReadUnitAimingVector(unit, unitAimForward);

                uint8_t parentKind = 0;
                unsigned char* parentData = ReachVehicleObjectData(info.directParent, parentKind);
                if (!parentData || parentKind != kReachObjectKindVehicle)
                {
                    identityMiss = true;
                    break;
                }
                const uint32_t definitionDatum =
                    *reinterpret_cast<const uint32_t*>(parentData + kReachObjectDefinitionDatumOffset);
                auto* definition =
                    static_cast<unsigned char*>(g_reachCamera.tagGet(kReachVehicleGroupTag, definitionDatum));
                if (!definition)
                {
                    identityMiss = true;
                    break;
                }
                ReachVehicleFingerprint fingerprint{};
                int32_t observedType = -1;
                const ReachVehicleId identity =
                    ReachResolveVehicleDefinition(definition, definitionDatum, &fingerprint, &observedType);

                const int32_t seatCount = *reinterpret_cast<const int32_t*>(definition + kReachVehicleSeatsBlockOffset);
                const uint32_t packedSeats =
                    *reinterpret_cast<const uint32_t*>(definition + kReachVehicleSeatsBlockOffset + 4);
                // Halo 3 and ODST admit any seat whose loaded layout proves
                // itself and key only trims/policy on identity; Reach now
                // matches that experience. An unmatched tuple or an
                // out-of-census seat keeps the camera, with universal trims
                // and no-follow/no-steer policy for unmatched content, and is
                // named loudly (tuple, type, seat) by the worker exactly once.
                // Every structural proof below still gates the frame.
                if (identity == ReachVehicleId::Unknown || !ReachVehicleSeatIsPlayer(identity, info.seatIndex))
                {
                    ReachPublishVehicleCensusMiss(
                        fingerprint, observedType, info.seatIndex, seatCount, static_cast<uint8_t>(identity));
                }
                if (seatCount <= 0 || seatCount > kReachVehicleSeatLimit || info.seatIndex >= seatCount)
                {
                    identityMiss = true;
                    break;
                }
                unsigned char* seat = ReachPackedTagBlockElement(packedSeats, info.seatIndex, kReachVehicleSeatStride);
                if (!seat || seat + kReachSeatCameraOffset != info.seatCamera)
                {
                    identityMiss = true;
                    break;
                }
                const uint32_t seatFlags = *reinterpret_cast<const uint32_t*>(seat + kReachSeatFlagsOffset);
                const int32_t cameraMarkerSid = *reinterpret_cast<const int32_t*>(seat + kReachSeatCameraMarkerOffset);
                const int32_t attachmentMarkerSid =
                    *reinterpret_cast<const int32_t*>(seat + kReachSeatAttachmentMarkerOffset);

                const bool interpolate = g_config.vehicle_cam_smoothing;
                Halo3ObjectMarker rootMarker{};
                if (g_reachCamera.objectMarkers(info.directParent, 0, &rootMarker, 1, true, interpolate) <= 0 ||
                    !Halo3MatrixValid(rootMarker.matrix))
                {
                    markerFailure = true;
                    break;
                }
                Halo3ObjectMarker anchorMarker{};
                bool anchorValid = false;
                if (cameraMarkerSid != 0 && cameraMarkerSid != -1)
                {
                    anchorValid = g_reachCamera.objectMarkers(
                                      info.directParent, cameraMarkerSid, &anchorMarker, 1, true, interpolate) > 0 &&
                                  Halo3MatrixValid(anchorMarker.matrix);
                }
                if (!anchorValid && attachmentMarkerSid != 0 && attachmentMarkerSid != -1)
                {
                    anchorValid = g_reachCamera.objectMarkers(info.directParent, attachmentMarkerSid, &anchorMarker, 1,
                                      true, interpolate) > 0 &&
                                  Halo3MatrixValid(anchorMarker.matrix);
                }
                if (!anchorValid)
                {
                    // space_phantom_beam_turret authors neither a camera nor
                    // an attachment marker, and unmatched content may not
                    // either. The identity-root basis was already proven
                    // valid above, so seat placement degrades to the root
                    // point plus trims instead of losing the whole camera.
                    anchorMarker = rootMarker;
                    anchorValid = true;
                }
                // An unmatched vehicle keys the dedicated unmatched row
                // rather than -1. -1 meant "the universal trim", so both the
                // camera AND the F1 sliders bound to the base every other seat
                // in all three titles follows, and tuning that seat moved them
                // all.
                const int trimSlot = ConfigReachSeatTrimSlot(
                    identity != ReachVehicleId::Unknown ? static_cast<int>(identity) : kReachUnmatchedVehicleTrimId,
                    info.seatIndex);
                if (identity != ReachVehicleId::Unknown && trimSlot < 0)
                {
                    identityMiss = true;
                    break;
                }
                const float trimForward = ConfigReachSeatCamForward(g_config, trimSlot) * kReachWorldUnitsPerMeter;
                const float trimRight = ConfigReachSeatCamRight(g_config, trimSlot) * kReachWorldUnitsPerMeter;
                const float trimUp = ConfigReachSeatCamUp(g_config, trimSlot) * kReachWorldUnitsPerMeter;
                // Blender and the config use the identity-root frame, not a
                // possibly tilted camera marker: +X forward, +Y left, +Z up.
                // The seat is parented to the CAR: the anchor marker's offset
                // in the rendered root frame is latched once per occupation
                // and the camera rides the rigid root body from then on.
                // Following the live marker instead let the vehicle/occupant
                // animation graphs (which sway the sit markers while driving)
                // drag the view around the cabin - the motion Halo 3's
                // accepted design removed by anchoring to the rigid seat.
                ReachSeatAnchorLatch& anchorLatch = g_reachSeatAnchorLatch;
                const bool anchorLatchMatches =
                    anchorLatch.valid && anchorLatch.generation == generation && anchorLatch.unitHandle == unit &&
                    anchorLatch.directParent == info.directParent && anchorLatch.seatIndex == info.seatIndex &&
                    anchorLatch.identity == identity && anchorLatch.interpolateNodes == interpolate;
                if (!anchorLatchMatches)
                {
                    anchorLatch = {};
                    if (Halo3MatrixInverseTransformPoint(
                            rootMarker.matrix, anchorMarker.matrix.position, anchorLatch.anchorLocal))
                    {
                        anchorLatch.generation = generation;
                        anchorLatch.unitHandle = unit;
                        anchorLatch.directParent = info.directParent;
                        anchorLatch.seatIndex = info.seatIndex;
                        anchorLatch.identity = identity;
                        anchorLatch.interpolateNodes = interpolate;
                        anchorLatch.valid = true;
                    }
                }
                float anchorWorld[3]{};
                const bool anchorRigid = anchorLatch.valid && Halo3MatrixTransformPoint(rootMarker.matrix,
                                                                  anchorLatch.anchorLocal, anchorWorld);
                if (!anchorRigid)
                {
                    // Rigid parenting could not be proven this frame; the
                    // animated marker point is strictly better than losing
                    // the camera, and the worker names the fallback once.
                    memcpy(anchorWorld, anchorMarker.matrix.position, sizeof(anchorWorld));
                    anchorFallback = true;
                }
                Halo3Matrix4x3 seatFrame = rootMarker.matrix;
                memcpy(seatFrame.position, anchorWorld, sizeof(seatFrame.position));
                const float authoredLocal[3] = {trimForward, -trimRight, trimUp};
                ReachSeatCameraBasis authoredBasis{};
                authoredBasis.scale = rootMarker.matrix.scale;
                memcpy(authoredBasis.forward, rootMarker.matrix.forward, sizeof(authoredBasis.forward));
                memcpy(authoredBasis.left, rootMarker.matrix.left, sizeof(authoredBasis.left));
                memcpy(authoredBasis.up, rootMarker.matrix.up, sizeof(authoredBasis.up));
                float handsBase[3]{};
                if (!ReachComposeSeatCameraPoint(anchorWorld, authoredBasis, trimForward, trimRight, trimUp, handsBase))
                {
                    markerFailure = true;
                    break;
                }
                float boundsRadius = 0.0f;
                float boundsLocal[3]{};
                memcpy(&boundsRadius, definition + kReachVehicleBoundsRadiusOffset, sizeof(boundsRadius));
                memcpy(boundsLocal, definition + kReachVehicleBoundsOffsetX, sizeof(boundsLocal));
                // The guard exists to catch a point composed in a wrong frame
                // (tens of wu off), not to veto the authored seat range: the
                // Reach bank deliberately allows trims far outside any bounding
                // sphere (the authored Sabre eye is 42 m ahead of its marker).
                // Hold the pre-trim anchor to the 2 wu margin and widen the
                // sphere by exactly the trim this frame applies.
                const float boundsMarginUnits =
                    2.0f +
                    authoredBasis.scale * sqrtf(trimForward * trimForward + trimRight * trimRight + trimUp * trimUp);
                float boundsWorld[3]{};
                if (!Halo3MatrixTransformPoint(rootMarker.matrix, boundsLocal, boundsWorld) ||
                    !Halo3AnchorWithinBounds(handsBase, boundsWorld, boundsRadius, boundsMarginUnits))
                {
                    boundsFailure = true;
                    break;
                }

                frame.unitHandle = unit;
                frame.directParent = info.directParent;
                frame.carrier = info.directParent;
                frame.seatIndex = info.seatIndex;
                frame.definitionDatum = definitionDatum;
                frame.identity = identity;
                frame.seatFlags = seatFlags;
                frame.unitAimValid = unitAimValid;
                frame.entryForwardValid = true;
                memcpy(frame.entryForward, rootMarker.matrix.forward, sizeof(frame.entryForward));
                if (unitAimValid)
                {
                    memcpy(frame.unitAimForward, unitAimForward, sizeof(frame.unitAimForward));
                }
                memcpy(frame.handsBase, handsBase, sizeof(frame.handsBase));
                memcpy(frame.cameraBase, handsBase, sizeof(frame.cameraBase));

                // Reach's native first-person helper resolves marker SID 0xC2
                // on the occupant. Retain only its movement relative to a
                // settled seat-local pose, exactly like the accepted H3/ODST
                // bounce contract; until settled the rigid authored point wins.
                Halo3ObjectMarker headMarker{};
                const bool bounceRequested = fabsf(g_config.vehicle_bounce) > 1e-6f;
                if (!bounceRequested)
                    g_reachHeadReference = {};
                const bool headMarkerValid =
                    !bounceRequested ||
                    (g_reachCamera.objectMarkers(unit, static_cast<int32_t>(kReachOccupantHeadMarkerStringId),
                         &headMarker, 1, false, interpolate) > 0 &&
                        Halo3MatrixValid(headMarker.matrix));
                if (bounceRequested && headMarkerValid)
                {
                    ReachHeadReference& head = g_reachHeadReference;
                    const bool same = head.generation == generation && head.unitHandle == unit &&
                                      head.directParent == info.directParent && head.seatIndex == info.seatIndex &&
                                      head.identity == identity && head.interpolateNodes == interpolate;
                    if (!same)
                    {
                        head = {};
                        head.generation = generation;
                        head.unitHandle = unit;
                        head.directParent = info.directParent;
                        head.seatIndex = info.seatIndex;
                        head.identity = identity;
                        head.interpolateNodes = interpolate;
                    }
                    float currentHeadLocal[3]{};
                    const bool headLocalValid =
                        Halo3MatrixInverseTransformPoint(seatFrame, headMarker.matrix.position, currentHeadLocal);
                    if (!headLocalValid)
                    {
                        g_reachHeadReference = {};
                        bounceFallback = true;
                    }
                    else if (head.settle.Update(nowMs, currentHeadLocal))
                    {
                        float bouncedCamera[3]{};
                        if (Halo3ComputeHeadParentedPoint(seatFrame, headMarker.matrix.position, head.settle.reference,
                                authoredLocal, g_config.vehicle_bounce, bouncedCamera) &&
                            Halo3AnchorWithinBounds(bouncedCamera, boundsWorld, boundsRadius, boundsMarginUnits))
                        {
                            memcpy(frame.cameraBase, bouncedCamera, sizeof(frame.cameraBase));
                        }
                        else
                        {
                            bounceFallback = true;
                        }
                    }
                }
                else if (bounceRequested)
                {
                    g_reachHeadReference = {};
                    bounceFallback = true;
                }

                if (identity == ReachVehicleId::Unknown || ReachVehicleIsWalkUpTurret(identity) ||
                    ReachVehicleIsAttachedWeapon(identity))
                {
                    // An attached gun always follows its ultimate carrier. A
                    // walk-up turret tag can also ship mounted on a moving
                    // carrier (the four falcon side guns author
                    // plasma_turret's exact tuple), so the engine's own
                    // parent chain decides: a real carrier grants the
                    // attached-weapon frame, a self-parented emplacement and
                    // unmatched content claim no hull frame - no view
                    // follow, no steering authorship, no wheel.
                    const int32_t carrier = (identity != ReachVehicleId::Unknown && g_reachCamera.objectUltimateParent)
                                                ? g_reachCamera.objectUltimateParent(info.directParent)
                                                : -1;
                    const bool wantsCarrier =
                        ReachVehicleIsAttachedWeapon(identity) ||
                        (ReachVehicleIsWalkUpTurret(identity) && carrier != -1 && carrier != info.directParent);
                    if (!wantsCarrier)
                    {
                        frame.carrierBasisValid = false;
                    }
                    else
                    {
                        Halo3ObjectMarker carrierRoot{};
                        frame.carrier = carrier;
                        frame.carrierBasisValid =
                            carrier != -1 &&
                            g_reachCamera.objectMarkers(carrier, 0, &carrierRoot, 1, true, interpolate) > 0 &&
                            Halo3MatrixValid(carrierRoot.matrix);
                        if (frame.carrierBasisValid)
                        {
                            memcpy(
                                frame.rawCarrierForward, carrierRoot.matrix.forward, sizeof(frame.rawCarrierForward));
                            memcpy(frame.entryForward, carrierRoot.matrix.forward, sizeof(frame.entryForward));
                            // Follow stays on the rendered carrierRoot
                            // already copied above, matching camera placement
                            // and accepted Halo 3. The disproven raw-node
                            // experiment is kept inert instead of deleted.
                            Halo3ObjectMarker carrierRaw{};
                        }
                        else
                        {
                            // This seat has a real carrier by construction.
                            // Its direct gun root is not a vehicle-heading
                            // substitute; retry the entry reset after the
                            // render-matched carrier root becomes available.
                            frame.entryForwardValid = false;
                            if (g_config.vehicle_view_follow && ReachVehicleIsAttachedWeapon(identity))
                            {
                                followFallback = true;
                            }
                        }
                    }
                }
                else
                {
                    frame.carrierBasisValid = true;
                    memcpy(frame.rawCarrierForward, rootMarker.matrix.forward, sizeof(frame.rawCarrierForward));
                    // Same dormant raw-node experiment for a direct hull. The
                    // active source remains rootMarker's rendered basis.
                    Halo3ObjectMarker rootRaw{};
                }
                frame.active = g_config.vehicle_first_person;
                complete = true;
            } while (false);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("sampling the Reach vehicle camera frame faulted");
            faulted = true;
        }

        const ReachVehicleInputState inputState =
            !unitKnown ? ReachVehicleInputState::Unknown
                       : (seatOccupationObserved ? ReachVehicleInputState::Vehicle : ReachVehicleInputState::OnFoot);
        g_reachVehicleInputSnapshot.store(ReachVehicleInputSnapshot(generation, inputState), std::memory_order_release);
        if (faulted)
        {
            g_reachVehicleTrimSnapshot.store(0, std::memory_order_release);
            ReachPublishShotOccupation(generation, nullptr);
            // The optional vehicle transaction is going stock for this title
            // generation; it must not leave a cleared seat word behind.
            ReachRestoreNativeSeatLease();
            g_reachVehicleCameraFaults.fetch_add(1, std::memory_order_relaxed);
            g_reachCamera.vehicleCameraBindingState.store(
                static_cast<uint8_t>(ReachVehicleCameraBindingState::StockFallback), std::memory_order_release);
            g_reachHeadReference = {};
            g_reachSeatAnchorLatch = {};
            g_reachVehicleFpDebounce = {};
            g_reachSeatRecenterLatch.Clear();
            g_reachVehicleFpStable.store(static_cast<uint32_t>(Halo3VehicleState::Unknown), std::memory_order_relaxed);
            return false;
        }
        if (identityMiss)
        {
            g_reachVehicleIdentityMisses.fetch_add(1, std::memory_order_relaxed);
        }
        if (markerFailure)
        {
            g_reachVehicleMarkerFailures.fetch_add(1, std::memory_order_relaxed);
        }
        if (boundsFailure)
        {
            g_reachVehicleBoundsFailures.fetch_add(1, std::memory_order_relaxed);
        }
        if (bounceFallback)
        {
            g_reachVehicleBounceFallbacks.fetch_add(1, std::memory_order_relaxed);
        }
        if (followFallback)
        {
            g_reachVehicleFollowFallbacks.fetch_add(1, std::memory_order_relaxed);
        }
        if (anchorFallback)
        {
            g_reachVehicleAnchorFallbacks.fetch_add(1, std::memory_order_relaxed);
        }
        if (!complete)
        {
            g_reachVehicleTrimSnapshot.store(0, std::memory_order_release);
            ReachPublishShotOccupation(generation, nullptr);
            g_reachHeadReference = {};
            // Give the seat word back the moment the seat relationship
            // itself is gone. A camera-only miss (marker/bounds/identity) with
            // the structural occupation intact keeps the lease, exactly as it
            // keeps the debounced occupation immediately below - otherwise
            // every transient miss would re-toggle the bit and reintroduce the
            // rejected per-frame churn.
            if (!structurallySeated)
                ReachRestoreNativeSeatLease();
            // Marker, bounds and identity proof can fail while the occupied
            // seat relationship itself remains structurally valid. Hold the
            // already-debounced occupation through those camera-only misses:
            // reporting OnFoot here created a false settled exit, cleared the
            // exact-seat latch and caused the same occupation to recenter
            // again on recovery. A not-yet-admitted entry stays pending until
            // a complete camera frame proves it.
            ReachUpdateVehicleTransition(
                !unitKnown ? Halo3VehicleState::Unknown
                           : (seatOccupationObserved ? g_reachVehicleFpDebounce.stable : Halo3VehicleState::OnFoot),
                nowMs);
            return false;
        }

        g_reachVehicleTrimSnapshot.store(
            frame.active ? ReachVehicleTrimSnapshot(generation, frame.identity, frame.seatIndex) : 0,
            std::memory_order_release);
        // Publish the seat's own flags word from the engine thread that
        // read it. An earlier report printed 00000000 for every seat because
        // it read g_reachOwnerScope from the 50 ms worker, and that scope is
        // only populated INSIDE an outer render callback - a diagnostic that
        // reported zero no matter what the tag actually said.
        g_reachVehicleSeatFlagsPublished.store(frame.active ? frame.seatFlags : 0u, std::memory_order_release);
        // Publish the full-salt occupation as one seqlock record. The firing
        // thread must match it against the independently completed rendered-eye
        // record before substituting a personal-weapon origin.
        ReachPublishShotOccupation(generation, frame.active ? &frame : nullptr);
        // The exact shape of Halo 3's accepted seat-flag sampler: while the
        // seat is occupied, tell Reach that seat is first person; the instant
        // it is not, give the word back. Idempotent - a seat we already own is
        // returned early without a write, so the bit stays continuously clear
        // for the whole occupation instead of toggling every frame.
        if (frame.active)
            ReachEnsureNativeFirstPersonSeat(frame);
        else
            ReachRestoreNativeSeatLease();
        ReachUpdateVehicleTransition(Halo3VehicleState::Vehicle, nowMs);
        return frame.active;
    }
} // namespace
namespace reach_bridge
{
    bool ReachVehicleFpActive()
    {
        if (!g_config.vehicle_first_person ||
            g_reachCamera.vehicleCameraBindingState.load(std::memory_order_acquire) !=
                static_cast<uint8_t>(ReachVehicleCameraBindingState::Installed) ||
            g_reachVehicleFpStable.load(std::memory_order_relaxed) != static_cast<uint32_t>(Halo3VehicleState::Vehicle))
        {
            return false;
        }
        const uint32_t generation = g_reachCamera.generation.load(std::memory_order_acquire);
        const uint64_t sampleMs = g_reachVehicleSampleMs.load(std::memory_order_relaxed);
        const uint64_t nowMs = GetTickCount64();
        const uint64_t snapshot = g_reachVehicleTrimSnapshot.load(std::memory_order_acquire);
        return generation && sampleMs && nowMs >= sampleMs && nowMs - sampleMs <= 500 &&
               static_cast<uint32_t>(snapshot >> 32) == generation;
    }

    bool ReachFollowContextValid(uint32_t& outGeneration)
    {
        outGeneration = 0;
        if (!ReachVehicleFpActive())
            return false;
        outGeneration = g_reachCamera.generation.load(std::memory_order_acquire);
        return outGeneration != 0;
    }
} // namespace reach_bridge
namespace
{
    bool ReachCurrentSeat(ReachVehicleId& identity, int& seatIndex)
    {
        identity = ReachVehicleId::Unknown;
        seatIndex = -1;
        if (!ReachVehicleFpActive())
            return false;
        const uint32_t generation = g_reachCamera.generation.load(std::memory_order_acquire);
        const uint64_t snapshot = g_reachVehicleTrimSnapshot.load(std::memory_order_acquire);
        if (static_cast<uint32_t>(snapshot >> 32) != generation)
            return false;
        const int id = static_cast<int>((snapshot >> 8) & 0xFFu);
        const int seat = static_cast<int>(snapshot & 0xFFu) - 1;
        if (id <= 0 || id > kReachVehicleIdentityCount || seat < 0 || seat >= kReachVehicleSeatLimit)
        {
            return false;
        }
        identity = static_cast<ReachVehicleId>(id);
        seatIndex = seat;
        return true;
    }
} // namespace
namespace reach_bridge
{
    bool ReachSeatAuthorsSteeringNow()
    {
        return g_reachSeatAuthorsSteering.load(std::memory_order_acquire) != 0 && ReachVehicleFpActive();
    }
} // namespace reach_bridge
namespace
{
    // Where the CAR is actually pointing, published from the same raw
    // hull forward the follow integrator eats, for every proven ground-driver
    // frame regardless of view follow. The follow-off hand steering closes its
    // loop on this instead of on a camera facing: a hand held still names one
    // fixed world heading, the vehicle turns onto it, and the command decays
    // to nothing, so a resting controller drives dead straight. Same
    // torn-write discipline as the roll-stable follow publication.
    struct ReachHullHeadingPublication
    {
        std::atomic<uint32_t> sequence{0};
        std::atomic<uint32_t> generation{0};
        std::atomic<uint64_t> sampleMs{0};
        std::atomic<float> yaw{0.0f};
        bool writerHasValue = false; // camera-thread writer only
    };
    constinit ReachHullHeadingPublication g_reachHullHeading;

    void ReachClearHullHeading()
    {
        auto& published = g_reachHullHeading;
        if (!published.writerHasValue)
            return;
        published.sequence.fetch_add(1, std::memory_order_acq_rel);
        published.writerHasValue = false;
        published.generation.store(0, std::memory_order_relaxed);
        published.sampleMs.store(0, std::memory_order_relaxed);
        published.yaw.store(0.0f, std::memory_order_relaxed);
        published.sequence.fetch_add(1, std::memory_order_release);
    }

    // Full stick deflection ~19 degrees off the wanted heading, and nothing at
    // all inside 3 degrees, so the hand does not have to be perfectly still to
    // hold a straight line. The vehicle's own turn rate is the real ceiling.
    inline constexpr float kReachHandSteerDeadbandRadians = 0.052f;
    inline constexpr float kReachHandSteerGain = 3.0f;
    // Names what owns the hull in this seat, so a driving report can be read
    // straight off the log instead of inferred from the config.
    const char* ReachSteeringModeName(ReachVehicleId identity, int seatIndex)
    {
        if (ReachVehicleSeatAuthorsSteering(identity, seatIndex, g_config.vehicle_view_follow))
        {
            return "stick/wheel steering; native unit aim; "
                   "render-matched view follows";
        }
        if (ReachVehicleSeatIsDriver(identity, seatIndex) && ReachVehicleUsesWheel(identity))
        {
            return "native unit-aim steering; stick turns the world-locked view";
        }
        return "native unit-aim weapon control";
    }
} // namespace
namespace reach_bridge
{
    bool ReachSeatUsesWheel()
    {
        if (!ReachSeatAuthorsSteeringNow())
            return false;
        ReachVehicleId identity = ReachVehicleId::Unknown;
        int seatIndex = -1;
        return ReachCurrentSeat(identity, seatIndex) && ReachVehicleUsesWheel(identity);
    }
} // namespace reach_bridge
namespace
{
    void ReachPublishGyroHull(uint32_t generation, uint32_t identity, const float forward[3])
    {
        GyroPublishHull(g_reachGyroHull, generation, identity, forward);
    }
    void ReachClearGyroHull()
    {
        GyroClearHull(g_reachGyroHull);
    }
} // namespace
namespace reach_bridge
{
    bool ReachReadGyroHull(turret_gyro::Hull& out, ReachVehicleId& identity)
    {
        uint32_t id = 0;
        const bool ok = GyroReadHull(g_reachGyroHull, out, id);
        identity = static_cast<ReachVehicleId>(id);
        return ok;
    }
} // namespace reach_bridge
namespace
{
    void ReachApplySeatYawFollow(const ReachVehicleFrame& frame)
    {
        // Wheel/Stick steering makes the driver seat follow the hull too.
        vehicle_wheel::SteerClass reachClass = vehicle_wheel::SteerClass::None;
        if (frame.active && ReachVehicleUsesWheel(frame.identity))
        {
            const ReachVehicleId id = frame.identity;
            reachClass = id == ReachVehicleId::Mongoose || id == ReachVehicleId::Ghost ? vehicle_wheel::SteerClass::Bike
                                                                                       : vehicle_wheel::SteerClass::Car;
        }
        const bool reachDriver = frame.active && ReachVehicleSeatIsDriver(frame.identity, frame.seatIndex);
        if (TitleAdapter_GetActiveTitle() == GameTitle::HaloReach) // on foot publishes "not the driver"
            PublishVehicleSteerContext(GameTitle::HaloReach, reachDriver, reachClass,
                frame.identity == ReachVehicleId::Ghost || frame.identity == ReachVehicleId::Revenant,
                frame.active && ReachVehicleIsAircraft(frame.identity));
        const bool reachFollow = vehicle_wheel::EffectiveFollow(g_config.vehicle_view_follow, reachDriver, reachClass,
            vehicle_wheel::ModeFor(reachClass, g_config.vehicle_steer_car, g_config.vehicle_steer_bike));
        const uint32_t generation = g_reachCamera.generation.load(std::memory_order_relaxed);
        // Ownership is follow-coupled, exactly as the accepted
        // Halo 3 title does it. Handing a ground driver's stick to the
        // hull unconditionally cured the resting-hand slide but left
        // follow-off driving with nothing to turn the view: the fourth headset
        // report, "the camera gets stuck to one spot... the whole mechanism is
        // not like Halo 3". Halo 3's accepted follow-off seat gives the stick
        // back to the VR view and steers the hull with the hand as a
        // CONVERGING heading target; Reach gets the same two halves.
        // Follow ON keeps the authored stick steering, which is also what
        // Halo 3 does with the switch on.
        const bool groundDriverAuthors = frame.active && reachFollow &&
                                         ReachVehicleSeatIsDriver(frame.identity, frame.seatIndex) &&
                                         ReachVehicleUsesWheel(frame.identity) && ReachVehicleFpActive();
        // A walk-up turret riding a real carrier (falcon side guns resolve to
        // plasma_turret's tuple) earned a carrier basis in the frame builder;
        // it follows that carrier's yaw exactly like an attached weapon.
        const bool mountedWalkUp = ReachVehicleIsWalkUpTurret(frame.identity) && frame.carrierBasisValid &&
                                   frame.carrier != frame.directParent;
        // Turret gyro: every mounted gun with a proven carrier basis, follow or not.
        if (frame.active && ReachVehicleFpActive() && frame.carrierBasisValid &&
            (ReachVehicleIsAttachedWeapon(frame.identity) || mountedWalkUp))
            ReachPublishGyroHull(generation, static_cast<uint32_t>(frame.identity), frame.rawCarrierForward);
        else
            ReachClearGyroHull();
        const bool follows = frame.active && ReachVehicleFpActive() && frame.carrierBasisValid && reachFollow &&
                             (ReachVehicleSeatFollowsHull(frame.identity, frame.seatIndex) || mountedWalkUp);
        if (!follows)
        {
            g_reachYawSeat = {};
            g_reachSeatYaw = {};
            g_reachSeatAuthorsSteering.store(groundDriverAuthors ? 1u : 0u, std::memory_order_release);
            Halo3ClearRollStableFollow();
            return;
        }
        const bool mounted = ReachVehicleIsAttachedWeapon(frame.identity) || mountedWalkUp;
        if (!g_reachYawSeat.Matches(
                generation, frame.carrier, frame.seatIndex, static_cast<uint32_t>(frame.identity), mounted))
        {
            g_reachYawSeat.Set(
                generation, frame.carrier, frame.seatIndex, static_cast<uint32_t>(frame.identity), mounted);
            g_reachSeatYaw = {};
        }
        const float delta = Halo3SeatYawDelta(g_reachSeatYaw, frame.rawCarrierForward, true);
        NoteVehicleFollowYaw(delta);
        if (delta != 0.0f)
            g_gameYawRef = WrapPi(g_gameYawRef + delta);

        float hullPitch = 0.0f;
        if (!g_config.vehicle_horizon_lock && ReachVehicleSeatFollowsPitch(frame.identity, frame.seatIndex) &&
            g_reachSeatYaw.armed && Halo3RollStablePitchFromForward(frame.rawCarrierForward, hullPitch))
        {
            Halo3PublishRollStableFollowFor(generation, VehicleFollowOwner::Reach, g_reachSeatYaw.lastYaw, hullPitch);
        }
        else
        {
            Halo3ClearRollStableFollow();
        }
        const bool authors =
            groundDriverAuthors || ReachVehicleSeatAuthorsSteering(frame.identity, frame.seatIndex, reachFollow);
        g_reachSeatAuthorsSteering.store(authors ? 1u : 0u, std::memory_order_release);
    }

    uintptr_t ReachMainRenderViewBody(uintptr_t workspace, uintptr_t playerView, uint32_t windowIndex,
        uintptr_t returnAddress, bool& seatLeaseCleanupPermitted)
    {
        seatLeaseCleanupPermitted = false;
        const ReachOwnerScope previous = g_reachOwnerScope;
        if (g_reachNestedOuterSuppressed)
            return g_reachOrigMainRenderView(workspace, playerView, windowIndex);
        if (previous.active)
        {
            // The camera stack can saturate at depth three. Leaving the parent
            // TLS token visible while a nested stock call runs could let its
            // inner renderer satisfy the parent's pointers/depth by accident.
            // Suppress the complete nested call tree. A nested stock FP
            // interpolation can reuse and overwrite the parent's live graph,
            // so preserve that bounded graph as well as the owner token and
            // restore it even if stock raises an SEH exception.
            uintptr_t nestedResult = 0;
            struct ParentGraphSnapshot
            {
                bool contextValid = false;
                bool graphSaved = false;
                BoneMatrix* graph = nullptr;
                int count = 0;
                BoneMatrix records[kReachFpMaxSourceNodeCount]{};
            } savedParents[kReachFpTransactionCapacity]{};
            for (size_t i = 0; i < kReachFpTransactionCapacity; ++i)
            {
                const ReachFpInterpolationContext& context = g_reachFpInterpolations[i];
                ParentGraphSnapshot& saved = savedParents[i];
                saved.contextValid = context.valid;
                saved.graph = context.source;
                saved.count = context.liveSourceCount;
                saved.graphSaved =
                    context.valid && saved.graph && saved.count > 0 &&
                    saved.count <= static_cast<int>(kReachFpMaxSourceNodeCount) &&
                    SafeReadBytes(saved.graph, saved.records, static_cast<size_t>(saved.count) * sizeof(BoneMatrix));
            }
            g_reachOwnerScope = {};
            g_reachNestedOuterSuppressed = true;
            __try
            {
                nestedResult = g_reachOrigMainRenderView(workspace, playerView, windowIndex);
            }
            __finally
            {
                for (size_t i = 0; i < kReachFpTransactionCapacity; ++i)
                {
                    const ParentGraphSnapshot& saved = savedParents[i];
                    if (saved.contextValid &&
                        (!saved.graphSaved || !SafeWriteBytes(saved.graph, saved.records,
                                                  static_cast<size_t>(saved.count) * sizeof(BoneMatrix))))
                        g_reachFpInterpolations[i] = {};
                }
                g_reachNestedOuterSuppressed = false;
                g_reachOwnerScope = previous;
            }
            return nestedResult;
        }

        const ReachModuleEpoch epoch{g_reachCamera.base, g_reachCamera.generation};
        const bool normalPlayerWindow0 =
            windowIndex == 0 && ClassifyReachOuterRenderCaller(epoch.moduleBase, kReachRetailImageSize,
                                    returnAddress) == ReachOuterRenderCaller::NormalPlayer;
        if (normalPlayerWindow0)
        {
            // This callback does not restore the seat word on the way
            // in. An earlier design did, and re-cleared it before main_render_view, so the
            // bit toggled twice per frame - the rejected shaking, and a state
            // too brief for the engine to build the occupant's first-person
            // models. Halo 3's accepted design clears once per occupation and
            // restores on the seat/exit/config/teardown edges, which the
            // sampler below and CleanupLocked now own. Abnormal exit still
            // restores through the __finally this flag guards.
            seatLeaseCleanupPermitted = true;
            // Teardown is the one case the sampler cannot serve: it returns at
            // its own first guard once teardown is requested, so without this
            // the worker would wait forever for an engine-thread restore and
            // retain the hook. False in ordinary play, so the steady state
            // still never touches the word.
            if (g_reachCamera.teardownRequested.load(std::memory_order_acquire) ||
                g_reachCamera.vehicleCameraBindingState.load(std::memory_order_acquire) !=
                    static_cast<uint8_t>(ReachVehicleCameraBindingState::Installed))
            {
                ReachRestoreNativeSeatLease();
            }
        }

        ReachSampleVehicleInputState(windowIndex, returnAddress);

        uintptr_t expectedWorkspace = 0;
        uintptr_t expectedPlayerView = 0;
        const ReachPreflightToken preflight = ReachRenderCandidate_GetPreflight(epoch);
        const ReachPreparedFrameToken prepared = VR_ReachPreparedFrame(epoch);
        if (!g_reachCamera.armed.load(std::memory_order_acquire) || !normalPlayerWindow0 || !g_reachHelpers.Ready() ||
            !ReachAddressFromRva(
                epoch.moduleBase, kReachRetailImageSize, kReachDefaultWorkspaceRva, expectedWorkspace) ||
            !ReachAddressFromRva(
                epoch.moduleBase, kReachRetailImageSize, kReachPlayerViewArrayRva, expectedPlayerView) ||
            workspace != expectedWorkspace || playerView != expectedPlayerView || !preflight.Complete() ||
            !ReachRenderCandidate_IsPreflightCurrent(preflight) || !prepared.Ready() || !VR_ReachDisplayReady(epoch))
        {
            return g_reachOrigMainRenderView(workspace, playerView, windowIndex);
        }

        ReachApplyMotionBlurSetting();

        ReachVrRenderSnapshot tracking{};
        if (!VR_ReachGetRenderSnapshot(prepared, tracking) || tracking.preparedSerial != prepared.Serial())
        {
            return g_reachOrigMainRenderView(workspace, playerView, windowIndex);
        }

        const int32_t stackDepthBefore =
            *reinterpret_cast<const int32_t*>(epoch.moduleBase + kReachCameraStackDepthRva);
        const uintptr_t expectedOwner = playerView + kReachPlayerViewCameraStateOffset;
        unsigned char* primaryCompact = reinterpret_cast<unsigned char*>(workspace);
        unsigned char* primaryDerived = reinterpret_cast<unsigned char*>(workspace + kReachPrimaryDerivedOffset);
        const unsigned char* secondaryCompact =
            reinterpret_cast<const unsigned char*>(workspace + kReachSecondaryCompactOffset);
        const unsigned char* secondaryDerived =
            reinterpret_cast<const unsigned char*>(workspace + kReachSecondaryDerivedOffset);
        ReachCompactCameraObservation observedPrimary{};
        ReachCompactCameraObservation observedSecondary{};
        if (stackDepthBefore < -1 || stackDepthBefore >= 3 ||
            *reinterpret_cast<const uintptr_t*>(epoch.moduleBase + kReachRenderCameraOwnerRva) != expectedOwner ||
            *reinterpret_cast<const uint32_t*>(epoch.moduleBase + kReachSelectedSpecializationRva) != 0 ||
            !ValidateReachCompactCamera(primaryCompact, kReachCompactCameraBytes, observedPrimary) ||
            !ValidateReachCompactCamera(secondaryCompact, kReachCompactCameraBytes, observedSecondary) ||
            memcmp(primaryCompact, secondaryCompact, kReachCompactCameraBytes) != 0 ||
            memcmp(primaryDerived, secondaryDerived, kReachDerivedBlockSize) != 0)
        {
            return g_reachOrigMainRenderView(workspace, playerView, windowIndex);
        }

        ReachVehicleFrame vehicleFrame{};
        const bool vehicleFrameReady = ReachSampleVehicleCameraFrame(vehicleFrame) && ReachVehicleFpActive();
        if (!vehicleFrameReady)
            vehicleFrame = {};
        ReachApplySeatYawFollow(vehicleFrame);
        const bool stableVehicle =
            g_reachVehicleFpStable.load(std::memory_order_relaxed) == static_cast<uint32_t>(Halo3VehicleState::Vehicle);
        const ReachSeatLeaseKey seatRecenterKey{epoch.generation, vehicleFrame.unitHandle, vehicleFrame.directParent,
            vehicleFrame.definitionDatum, vehicleFrame.seatIndex};
        const ReachSeatLeaseKey* currentSeatKey = vehicleFrameReady ? &seatRecenterKey : nullptr;
        const bool seatEntryRecenterPending = g_reachSeatRecenterLatch.NeedsApply(
            g_config.vehicle_first_person && g_config.vehicle_recenter_on_seat, stableVehicle, currentSeatKey);

        ReachOwnerScope candidate{};
        candidate.workspace = workspace;
        candidate.playerView = playerView;
        candidate.cameraStackDepthBefore = stackDepthBefore;
        candidate.preparedSerial = prepared.Serial();
        memcpy(candidate.gameplayBasePosition, primaryCompact + 0x00, sizeof(candidate.gameplayBasePosition));
        memcpy(candidate.presentedCameraBasePosition, primaryCompact + 0x00,
            sizeof(candidate.presentedCameraBasePosition));
        candidate.vehicle = vehicleFrame;
        const float* authoredProjection =
            reinterpret_cast<const float*>(primaryDerived + kReachDerivedProjectionOffset);
        const float authoredAspect =
            CutsceneTheaterAspectFromProjectionScales(authoredProjection[0], authoredProjection[5]);
        candidate.cutsceneTheaterAspect = authoredAspect;
        const uint32_t theaterGeneration = g_reachCamera.generation.load(std::memory_order_relaxed);
        if (theaterGeneration && authoredAspect > 0.0f)
        {
            TitleAdapter_PublishCutsceneTheaterProjection(
                GameTitle::HaloReach, theaterGeneration, authoredAspect, GetTickCount64());
        }
        ReachSymmetricFovCover cullCover{};
        if (!ReachCollectEyeInputs(observedPrimary.renderBounds, tracking, candidate.eyes, cullCover))
        {
            return g_reachOrigMainRenderView(workspace, playerView, windowIndex);
        }

        ReachVrRenderAccess access{};
        if (!VR_ReachBeginRenderAccess(epoch, prepared, access))
            return g_reachOrigMainRenderView(workspace, playerView, windowIndex);

        alignas(16) unsigned char headDerived[kReachDerivedBlockSize]{};
        bool headCullReady = false;
        __try
        {
            headCullReady = ReachBuildHeadCullCamera(primaryCompact, cullCover, tracking,
                vehicleFrameReady ? &candidate.vehicle : nullptr, seatEntryRecenterPending ? &seatRecenterKey : nullptr,
                candidate.pendingSeatRecenter, candidate.headCenter, headDerived, candidate.cutsceneTheaterAspect,
                candidate.cutsceneTheater, candidate.vehicleViewApplied);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("building the Reach head cull camera faulted");
            headCullReady = false;
        }
        const ReachPreparedFrameToken currentPrepared = VR_ReachPreparedFrame(epoch);
        if (!headCullReady || !currentPrepared.Ready() || currentPrepared.Serial() != candidate.preparedSerial ||
            access.preparedSerial != candidate.preparedSerial)
        {
            VR_ReachEndRenderAccess(access);
            return g_reachOrigMainRenderView(workspace, playerView, windowIndex);
        }

        ReachPrepareScope(candidate, headDerived, access);

        if (candidate.vehicleViewApplied)
        {
            const float* handOrigin =
                g_config.vehicle_hands_follow_body ? candidate.vehicle.handsBase : candidate.vehicle.cameraBase;
            memcpy(candidate.gameplayBasePosition, handOrigin, sizeof(candidate.gameplayBasePosition));
        }
        alignas(16) unsigned char savedWorkspace[kReachCameraPairDataSize];
        alignas(16) unsigned char savedPv[kReachPvSnapshotBytes];
        memcpy(savedWorkspace, reinterpret_cast<const void*>(workspace), kReachCameraPairDataSize);
        memcpy(savedPv, reinterpret_cast<const void*>(playerView + kReachPvSnapshotBegin), kReachPvSnapshotBytes);

        // Commit coherent head-centre pre-ownership state before the outer
        // function's visibility work. Visibility reads the secondary workspace
        // pair before player_view_render. Updating both with the exact proven
        // setup helpers prevents a claimed transaction from mixing head-owned
        // culling with old gun/aim-owned raster matrices before terminal rejection.
        bool headCommitReady = false;
        __try
        {
            memcpy(primaryCompact, candidate.headCenter, kReachCompactCameraBytes);
            memcpy(primaryDerived, headDerived, kReachDerivedBlockSize);
            memcpy(reinterpret_cast<void*>(workspace + kReachSecondaryCompactOffset), candidate.headCenter,
                kReachCompactCameraBytes);
            memcpy(
                reinterpret_cast<void*>(workspace + kReachSecondaryDerivedOffset), headDerived, kReachDerivedBlockSize);
            g_reachHelpers.cameraState(reinterpret_cast<void*>(expectedOwner), candidate.headCenter);
            g_reachHelpers.matrix(reinterpret_cast<void*>(playerView + kReachPlayerViewCurrentMatricesOffset),
                headDerived, headDerived + kReachDerivedProjectionOffset,
                candidate.headCenter + kReachCompactRenderBoundsOffset,
                reinterpret_cast<void*>(playerView + kReachPlayerViewProjectionOffsetPairOffset));
            *reinterpret_cast<uintptr_t*>(epoch.moduleBase + kReachRenderCameraOwnerRva) = expectedOwner;
            headCommitReady = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("writing the Reach head camera faulted");
            headCommitReady = false;
        }
        if (!headCommitReady)
        {
            memcpy(reinterpret_cast<void*>(workspace), savedWorkspace, kReachCameraPairDataSize);
            memcpy(reinterpret_cast<void*>(playerView + kReachPvSnapshotBegin), savedPv, kReachPvSnapshotBytes);
            *reinterpret_cast<uintptr_t*>(epoch.moduleBase + kReachRenderCameraOwnerRva) = expectedOwner;
            VR_ReachEndRenderAccess(access);
            return g_reachOrigMainRenderView(workspace, playerView, windowIndex);
        }
        if (candidate.pendingSeatRecenter.valid)
            ReachCommitPendingSeatRecenter(candidate.pendingSeatRecenter);
        // Input follows the last camera state that actually crossed the outer
        // commit. Rejected candidates never publish a half-rendered follow or
        // turn update.
        ReachPublishYawReferencePair(g_gameYawRef, g_headYawRef);
        {
            // The committed head-centre camera, for the shot lines.
            const float* p = reinterpret_cast<const float*>(candidate.headCenter + 0x00);
            const float* f = reinterpret_cast<const float*>(candidate.headCenter + 0x0C);
            const float* u = reinterpret_cast<const float*>(candidate.headCenter + 0x18);
            g_reachShotHead.sequence.fetch_add(1, std::memory_order_acq_rel);
            for (int i = 0; i < 3; ++i)
            {
                g_reachShotHead.pos[i].store(p[i]);
                g_reachShotHead.fwd[i].store(f[i]);
                g_reachShotHead.up[i].store(u[i]);
            }
            g_reachShotHead.sequence.fetch_add(1, std::memory_order_release);
        }
        // Controller targets consume the committed reference triplet. Building
        // them before the exact-seat commit made the first owned passenger
        // frame use the previous seat's hand origin even though headCenter
        // already used the new one.
        candidate.fpTargets = {};
        candidate.fpTargets.handAlignment = tracking.handAlignment;
        candidate.fpTargets.centerRootValid = ReachBuildCenterFpRoot(candidate.headCenter,
            candidate.vehicleViewApplied ? candidate.gameplayBasePosition : nullptr, candidate.fpTargets.centerRoot);
        candidate.fpTargets.rightWristValid = ReachBuildPreparedControllerTarget(tracking, false,
            candidate.gameplayBasePosition, candidate.fpTargets.rightWrist, candidate.fpTargets.rightScale,
            &candidate.fpTargets.contactSpace[1], &candidate.fpTargets.contactController[1]);
        candidate.fpTargets.leftWristValid = ReachBuildPreparedControllerTarget(tracking, true,
            candidate.gameplayBasePosition, candidate.fpTargets.leftWrist, candidate.fpTargets.leftScale,
            &candidate.fpTargets.contactSpace[0], &candidate.fpTargets.contactController[0]);
        // Grab pickup: the support hand in world units for the game's target
        // search (inert unless the switch is on and its hooks are proven).
        ReachGrabPublishHand(
            candidate.fpTargets.leftWristValid, candidate.fpTargets.contactSpace[0], tracking.leftControllerPosition);
        // Persistent support grip stale-owner fallback. Kept unwired with
        // the feature off (no build, no cost, exact base targets). The same
        // prepared-frame position/scale contract as rightWrist; only the
        // orientation source differs: the ordinary calibrated one-hand solve
        // the Reach frame already computed for this exact serial. When the FP
        // invocation later proves it is not the relationship owner, the visible
        // weapon hand is re-seated from this target instead of the
        // support-steered one. No new solve and no new fallback semantic is
        // introduced here.
        if (g_config.persistent_support_grip && tracking.rightPhysicalValid)
        {
            ReachVrRenderSnapshot oneHandTracking = tracking;
            memcpy(oneHandTracking.rightAimOrientation, tracking.rightPhysicalOrientation,
                sizeof(oneHandTracking.rightAimOrientation));
            oneHandTracking.rightAimValid = tracking.rightPhysicalValid;
            candidate.fpTargets.rightOneHandWristValid =
                ReachBuildPreparedControllerTarget(oneHandTracking, false, candidate.gameplayBasePosition,
                    candidate.fpTargets.rightOneHandWrist, candidate.fpTargets.rightOneHandScale);
        }
        candidate.fpTargets.contactSerial = tracking.preparedSerial;
        candidate.fpTargets.contactTimeNs = tracking.predictedDisplayTimeNs;
        if (tracking.trackingSpaceEpoch && !candidate.vehicleViewApplied && !candidate.cutsceneTheater)
        {
            uint64_t reference = (14695981039346656037ull ^ tracking.trackingSpaceEpoch) * 1099511628211ull;
            reference = (reference ^ uint64_t(epoch.generation)) * 1099511628211ull;
            // Grabbing/releasing support changes the derived weapon pose;
            // that transition must not look like an angular physical strike.
            reference = (reference ^ uint64_t(tracking.twoHandAimActive)) * 1099511628211ull;
            const float referenceInputs[]{g_headPosRef[0], g_headPosRef[1], g_headPosRef[2], g_headYawRef,
                g_config.gun_scale, g_config.left_hand_scale, g_config.gun_forward_m, g_config.gun_right_m,
                g_config.gun_up_m, g_config.left_hand_forward_m, g_config.gun_pitch_deg, g_config.gun_yaw_deg,
                g_config.gun_roll_deg, g_config.barrel_pitch_deg, g_config.barrel_yaw_deg, g_config.barrel_roll_deg};
            for (float value : referenceInputs)
            {
                uint32_t bits = 0;
                memcpy(&bits, &value, sizeof(bits));
                reference = (reference ^ bits) * 1099511628211ull;
            }
            candidate.fpTargets.contactReference = reference ? reference : 1;
            candidate.fpTargets.contactEpoch = tracking.trackingSpaceEpoch;
        }
        // Reach supplies explicit prepared wrist targets to the shared palette
        // solve, so it never passes through DesiredWristWorld (where H3/ODST
        // consume their correction).  Consume the prior bounded native result
        // at this exact outer-frame ownership boundary instead.  This changes
        // translation only; controller orientation, authored barrel alignment,
        // physical melee, and every failed/disabled feature path stay intact.
        if (candidate.fpTargets.rightWristValid)
            (void)LegacyApplyWorldCollision(
                GameTitle::HaloReach, 1, candidate.fpTargets.rightWrist, candidate.fpTargets.collisionCorrection[1]);
        // The one-hand fallback target obeys the same collision correction as
        // the attached target, but keeps its own result out of the published
        // correction: that field belongs to the served target.
        if (candidate.fpTargets.rightOneHandWristValid)
        {
            float oneHandCorrection[3]{};
            (void)LegacyApplyWorldCollision(
                GameTitle::HaloReach, 1, candidate.fpTargets.rightOneHandWrist, oneHandCorrection);
        }
        if (candidate.fpTargets.leftWristValid)
            (void)LegacyApplyWorldCollision(
                GameTitle::HaloReach, 0, candidate.fpTargets.leftWrist, candidate.fpTargets.collisionCorrection[0]);
        candidate.fpTargets.twoHandAimActive = tracking.twoHandAimActive;
        candidate.renderAccess = &access;
        candidate.active = true;
        g_reachOwnerScope = candidate;
        // Reach builds the first-person interpolation graph and visible
        // palettes in main_render_view before it enters player_view_render.
        // Freeze the prepared-frame targets/layouts at this admitted outer
        // boundary so that pre-inner work can learn or apply the exact body
        // layout, and keep the same scope across both stereo eyes.
        ReachBeginFpPairScope(
            epoch.generation, candidate.preparedSerial, tracking, candidate.fpTargets, candidate.vehicleViewApplied);

        uintptr_t result = 0;
        ReachUnitCameraHideScope bodyHide{};
        if (candidate.vehicleViewApplied)
            ReachBeginUnitCameraPlayerHide(candidate.vehicle, bodyHide);
        __try
        {
            result = g_reachOrigMainRenderView(workspace, playerView, windowIndex);
        }
        __finally
        {
            ReachEndUnitCameraPlayerHide(bodyHide);
            ReachEndFpPairScope();
            // Restore only the proven camera-pair data. The engine owns the
            // camera-stack callback at +0x2A8 and its push/pop lifetime.
            const uint8_t finalLastWindow = *reinterpret_cast<const uint8_t*>(playerView + kReachLastWindowFlagOffset);
            memcpy(reinterpret_cast<void*>(workspace), savedWorkspace, kReachCameraPairDataSize);
            memcpy(reinterpret_cast<void*>(playerView + kReachPvSnapshotBegin), savedPv, kReachPvSnapshotBytes);
            *reinterpret_cast<uint8_t*>(playerView + kReachLastWindowFlagOffset) = finalLastWindow;
            g_reachOwnerScope = previous;
            VR_ReachEndRenderAccess(access);
        }
        return result;
    }

    __declspec(noinline) uintptr_t __fastcall ReachMainRenderViewDetour(
        uintptr_t workspace, uintptr_t playerView, uint32_t windowIndex)
    {
        const uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
        uintptr_t result = 0;
        bool bodyCompleted = false;
        bool seatLeaseCleanupPermitted = false;
        g_reachCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            result =
                ReachMainRenderViewBody(workspace, playerView, windowIndex, returnAddress, seatLeaseCleanupPermitted);
            bodyCompleted = true;
        }
        __finally
        {
            if (!bodyCompleted && seatLeaseCleanupPermitted)
                ReachRestoreNativeSeatLease();
            g_reachCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
        return result;
    }

#include "../../core/native_subtitles_reach_sources.inl"
    bool ScanForReachDetourIngress(bool& busy)
    {
        static bool rangesResolved = false;
        static ReachDetourCodeRange ranges[18]{};
        if (!rangesResolved)
        {
            const void* functions[] = {
                reinterpret_cast<const void*>(&ReachMainRenderViewDetour),
                reinterpret_cast<const void*>(&ReachPlayerViewRenderDetour),
                reinterpret_cast<const void*>(&ReachFpInterpolate),
                reinterpret_cast<const void*>(&ReachFpPalette),
                reinterpret_cast<const void*>(&ReachFpCameraRebuildDetour),
                reinterpret_cast<const void*>(&ReachRenderSsaoDetour),
                reinterpret_cast<const void*>(&ReachHudDrawWidgetDetour),
                reinterpret_cast<const void*>(&ReachObserverCameraDetour),
                reinterpret_cast<const void*>(&ReachEffectLocationDetour),
                reinterpret_cast<const void*>(&ReachRainRenderDetour),
                reinterpret_cast<const void*>(&ReachUnitCameraPositionHook),
                reinterpret_cast<const void*>(&ReachUnitAdjustHook),
                reinterpret_cast<const void*>(&ReachFirstPersonRenderGateHook),
                reinterpret_cast<const void*>(&ReachHudXformDetour),
                reinterpret_cast<const void*>(&ReachCollisionVectorDetour),
                reinterpret_cast<const void*>(&ReachContactUpdateDetour),
                reinterpret_cast<const void*>(&ReachContactDamageDetour),
                reinterpret_cast<const void*>(&ReachHudAnchorBasisDetour),
            };
            static_assert(_countof(functions) == _countof(ranges));
            bool resolved = true;
            for (size_t i = 0; i < _countof(functions); ++i)
                resolved &= ResolveReachDetourCodeRange(functions[i], ranges[i]);
            if (!resolved)
                return false;
            rangesResolved = true;
        }

        // Snapshot these worker-owned fields before suspending anything. They
        // cannot change until this cleanup call either removes or retains them.
        void* const targets[] = {
            g_reachCamera.outerTarget,
            g_reachCamera.innerTarget,
            g_reachCamera.fpInterpolateTarget,
            g_reachCamera.fpPaletteTarget,
            g_reachCamera.fpCameraTarget,
            g_reachCamera.ssaoTarget,
            g_reachCamera.hudDrawWidgetTarget,
            g_reachCamera.observerCameraTarget,
            g_reachCamera.effectLocationTarget,
            g_reachCamera.rainRenderTarget,
            g_reachUnitCameraPositionTarget,
            g_reachUnitAdjustTarget,
            g_reachFirstPersonRenderGateTarget,
            g_reachHudXformTarget,
            g_reachWorldCollision.target,
            g_reachContact.targets[1],
            g_reachContact.targets[0],
            g_reachHudAnchorBasisTarget,
        };
        void* const trampolines[] = {
            reinterpret_cast<void*>(g_reachOrigMainRenderView),
            reinterpret_cast<void*>(g_reachOrigPlayerViewRender),
            reinterpret_cast<void*>(g_reachOrigFpInterpolate),
            reinterpret_cast<void*>(g_reachOrigFpPalette),
            reinterpret_cast<void*>(g_reachOrigFpCameraRebuild),
            reinterpret_cast<void*>(g_reachOrigRenderSsao),
            reinterpret_cast<void*>(g_reachOrigHudDrawWidget),
            reinterpret_cast<void*>(g_reachOrigObserverCamera),
            reinterpret_cast<void*>(g_reachOrigEffectLocation),
            reinterpret_cast<void*>(g_reachOrigRainRender),
            reinterpret_cast<void*>(g_origReachUnitCameraPosition),
            reinterpret_cast<void*>(g_origReachUnitAdjust),
            reinterpret_cast<void*>(g_origReachFirstPersonRenderGate),
            reinterpret_cast<void*>(g_origReachHudXform),
            g_reachWorldCollision.original,
            reinterpret_cast<void*>(g_reachContact.original),
            reinterpret_cast<void*>(g_reachContact.damageOriginal),
            reinterpret_cast<void*>(g_reachOrigHudAnchorBasis),
        };
        static_assert(_countof(targets) == _countof(ranges));
        static_assert(_countof(trampolines) == _countof(ranges));

        ReachThreadFreeze frozenThreads;
        if (!frozenThreads.Capture())
            return false;

        busy = false;
        bool scanOk = true;
        for (const ReachFrozenThread& thread : frozenThreads.Threads())
        {
            if (!thread.suspended)
                continue;
            CONTEXT context{};
            context.ContextFlags = CONTEXT_CONTROL;
            if (!GetThreadContext(thread.handle, &context))
            {
                if (WaitForSingleObject(thread.handle, 0) != WAIT_OBJECT_0)
                    scanOk = false;
                continue;
            }

            const DWORD64 instruction = context.Rip;
            for (size_t i = 0; i < _countof(ranges); ++i)
            {
                if (!targets[i])
                    continue;
                if (instruction >= ranges[i].begin && instruction < ranges[i].end)
                {
                    busy = true;
                }

                // MinHook v1.3.4 x64 keeps both trampoline and relay in the
                // 64-byte slot rooted at ppOriginal. The relay can be entered
                // before the wrapper increments activeCallbacks.
                const DWORD64 trampoline = reinterpret_cast<DWORD64>(trampolines[i]);
                if (!trampoline)
                {
                    scanOk = false;
                    continue;
                }
                if (instruction >= trampoline && instruction < trampoline + 64)
                {
                    busy = true;
                }
            }
        }
        if (g_reachCamera.activeCallbacks.load(std::memory_order_acquire) != 0)
            busy = true;
        if (g_reachWorldCollision.callbacks.load(std::memory_order_acquire) != 0)
            busy = true;

        // Hooks are disabled and every other live thread remains suspended
        // through this RIP/counter snapshot, so an all-clear survives resume.
        const bool resumed = frozenThreads.Release();
        return scanOk && resumed;
    }

    bool WaitForReachDetourQuiescence()
    {
        for (unsigned waited = 0; waited < 2000; ++waited)
        {
            if (g_reachCamera.activeCallbacks.load(std::memory_order_acquire) == 0 &&
                g_reachWorldCollision.callbacks.load(std::memory_order_acquire) == 0)
            {
                bool ingressBusy = false;
                if (!ScanForReachDetourIngress(ingressBusy))
                {
                    LOG("Reach camera cleanup: detour ingress scan failed");
                    return false;
                }
                if (!ingressBusy && g_reachCamera.activeCallbacks.load(std::memory_order_acquire) == 0 &&
                    g_reachWorldCollision.callbacks.load(std::memory_order_acquire) == 0)
                    return true;
            }
            Sleep(1);
        }
        LOG("Reach camera cleanup: callbacks did not reach verified quiescence");
        return false;
    }

    // Reach hook removal, in the order DisableAndRemoveReachHooks has always used.
    // Each slot names its MinHook target, what to clear after a confirmed removal
    // (trampoline and dependent pointers), and its failure message.
    // The two contact-melee hooks are not rows here; they retire as
    // their own hook set (RemoveReachContactMelee) before this table runs.
    enum class HookRemoveLog
    {
        Status,
        TargetStatus,
        Plain
    };
    struct ReachHookSlot
    {
        void** target;
        void (*clearAfterRemove)();
        HookRemoveLog logKind;
        const char* failureMessage;
    };
    // not constinit: MSVC rejects it (C2127); read only after DLL init, so dynamic init order is irrelevant.
    const ReachHookSlot kReachHookRemovalOrder[] = {
        {&g_reachHudAnchorBasisTarget, [] { g_reachOrigHudAnchorBasis = nullptr; }, HookRemoveLog::Status,
            "Reach HUD height cleanup: hook remove failed (%d)"},
        {&g_reachWorldCollision.target,
            []
            {
                g_reachWorldCollision.original = nullptr;
                g_reachWorldCollision.resolver = nullptr;
            },
            HookRemoveLog::TargetStatus, "Reach world collision cleanup: hook remove failed for %p (%d)"},
        {&g_reachHudXformTarget, [] { g_origReachHudXform = nullptr; }, HookRemoveLog::Plain,
            "Reach brightness cleanup: hook remove failed"},
        {&g_reachFirstPersonRenderGateTarget, [] { g_origReachFirstPersonRenderGate = nullptr; }, HookRemoveLog::Plain,
            "Reach passenger first-person cleanup: render-gate hook remove failed"},
        {&g_reachUnitAdjustTarget, [] { g_origReachUnitAdjust = nullptr; }, HookRemoveLog::Plain,
            "Reach vehicle shot-line cleanup: unit-adjust hook remove failed"},
        {&g_reachUnitCameraPositionTarget, [] { g_origReachUnitCameraPosition = nullptr; }, HookRemoveLog::Plain,
            "Reach seat aim cleanup: firing-origin hook remove failed"},
        {&g_reachCamera.rainRenderTarget, [] { g_reachOrigRainRender = nullptr; }, HookRemoveLog::TargetStatus,
            "Reach rain cleanup: remove failed for %p (%d)"},
        {&g_reachCamera.effectLocationTarget,
            []
            {
                g_reachOrigEffectLocation = nullptr;
                g_reachEffectFpMarkerQuery = nullptr;
            },
            HookRemoveLog::TargetStatus, "Reach muzzle cleanup: remove failed for %p (%d)"},
        {&g_reachCamera.observerCameraTarget, [] { g_reachOrigObserverCamera = nullptr; }, HookRemoveLog::TargetStatus,
            "Reach head-lock cleanup: remove failed for %p (%d)"},
        {&g_reachCamera.hudDrawWidgetTarget, [] { g_reachOrigHudDrawWidget = nullptr; }, HookRemoveLog::TargetStatus,
            "Reach crosshair cleanup: remove failed for %p (%d)"},
        {&g_reachCamera.ssaoTarget,
            []
            {
                g_reachCamera.ssaoIsolationActive.store(false, std::memory_order_release);
                g_reachOrigRenderSsao = nullptr;
            },
            HookRemoveLog::TargetStatus, "Reach SSAO cleanup: remove failed for %p (%d)"},
        {&g_reachCamera.fpCameraTarget,
            []
            {
                g_reachOrigFpCameraRebuild = nullptr;
                g_reachFpCameraUpload = nullptr;
            },
            HookRemoveLog::TargetStatus, "Reach FP camera cleanup: remove failed for %p (%d)"},
        {&g_reachCamera.fpPaletteTarget, [] { g_reachOrigFpPalette = nullptr; }, HookRemoveLog::TargetStatus,
            "Reach FP palette cleanup: remove failed for %p (%d)"},
        {&g_reachCamera.fpInterpolateTarget, [] { g_reachOrigFpInterpolate = nullptr; }, HookRemoveLog::TargetStatus,
            "Reach FP interpolation cleanup: remove failed for %p (%d)"},
        {&g_reachCamera.innerTarget, [] { g_reachOrigPlayerViewRender = nullptr; }, HookRemoveLog::TargetStatus,
            "Reach camera cleanup: inner remove failed for %p (%d)"},
        {&g_reachCamera.outerTarget, [] { g_reachOrigMainRenderView = nullptr; }, HookRemoveLog::TargetStatus,
            "Reach camera cleanup: outer remove failed for %p (%d)"},
    };

    void LogReachHookRemoveFailure(const ReachHookSlot& slot, void* target, MH_STATUS status)
    {
        switch (slot.logKind)
        {
        case HookRemoveLog::Status:
            LOG(slot.failureMessage, static_cast<int>(status));
            break;
        case HookRemoveLog::TargetStatus:
            LOG(slot.failureMessage, target, static_cast<int>(status));
            break;
        case HookRemoveLog::Plain:
            LOG("%s", slot.failureMessage);
            break;
        }
    }

    bool DisableAndRemoveReachHooks()
    {
        g_reachHudHeightEnabled.store(false, std::memory_order_release);
        // Make both optional firing detours stock pass-throughs before any
        // disable attempt. Their trampolines and original callees point into
        // the title DLL, so both remain in the shared verified lifecycle.
        g_reachVehicleShotRedirectEnabled.store(false, std::memory_order_release);
        g_reachFirstPersonRenderGateReturn.store(0, std::memory_order_release);
        g_reachWorldCollision.runtimeBoundsProven.store(false, std::memory_order_release);
        g_reachWorldCollision.installed.store(false, std::memory_order_release);
        RevokeReachFiringOriginFeature();
        bool disabledAll = true;
        void* const targets[] = {
            g_reachHudAnchorBasisTarget,
            g_reachWorldCollision.target,
            g_reachHudXformTarget,
            g_reachFirstPersonRenderGateTarget,
            g_reachUnitAdjustTarget,
            g_reachUnitCameraPositionTarget,
            g_reachCamera.rainRenderTarget,
            g_reachCamera.effectLocationTarget,
            g_reachCamera.observerCameraTarget,
            g_reachCamera.hudDrawWidgetTarget,
            g_reachCamera.ssaoTarget,
            g_reachCamera.outerTarget,
            g_reachCamera.innerTarget,
            g_reachCamera.fpInterpolateTarget,
            g_reachCamera.fpPaletteTarget,
            g_reachCamera.fpCameraTarget,
        };
        for (void* target : targets)
        {
            if (!target)
                continue;
            const MH_STATUS status = MH_DisableHook(target);
            if (!HookDisableStatusIsSafe(status))
            {
                disabledAll = false;
                LOG("Reach camera cleanup: disable failed for %p (%d)", target, static_cast<int>(status));
            }
        }
        if (!disabledAll || !WaitForReachDetourQuiescence())
            return false;

        // Remove in the established order; each slot clears its own trampoline and
        // dependent pointers only once MinHook confirms the detour is gone.
        bool removedAll = true;
        for (const ReachHookSlot& slot : kReachHookRemovalOrder)
        {
            void* const target = *slot.target;
            if (!target)
                continue;
            const MH_STATUS status = MCCVR_RemoveHookChecked(target);
            if (status == MH_OK || status == MH_ERROR_NOT_CREATED)
            {
                *slot.target = nullptr;
                slot.clearAfterRemove();
            }
            else
            {
                removedAll = false;
                LogReachHookRemoveFailure(slot, target, status);
            }
        }
        return removedAll;
    }

    bool RemoveReachCameraCore()
    {
        if (!RemoveReachGrabPickup())
            return false;
        if (!RemoveReachWeaponHaptics())
            return false;
        if (!RemoveReachMuzzle())
            return false;
        // Contact melee retires as its own hook set, as in Halo 2 and Halo 4,
        // before the camera table (which no longer lists its two hooks). Busy or
        // a failed removal keeps both for the next teardown attempt.
        if (!RemoveReachContactMelee())
            return false;
        TitleAdapter_ClearCinematicControl(
            GameTitle::HaloReach, g_reachCamera.generation.load(std::memory_order_acquire));
        // Revoke the optional input publication before detour quiescence. Any
        // concurrent XInput read immediately falls back to the established
        // Reach-wide swap; the retained function pointers are cleared only
        // after every outer-render callback has returned.
        g_reachCamera.vehicleInputBindingState.store(
            static_cast<uint8_t>(ReachVehicleInputBindingState::CleanupRequired), std::memory_order_release);
        g_reachVehicleInputSnapshot.store(0, std::memory_order_release);
        g_reachCamera.vehicleCameraBindingState.store(
            static_cast<uint8_t>(ReachVehicleCameraBindingState::CleanupRequired), std::memory_order_release);
        g_reachVehicleTrimSnapshot.store(0, std::memory_order_release);
        g_reachSeatAuthorsSteering.store(0, std::memory_order_release);
        Halo3ClearRollStableFollow();
        ReachClearHullHeading();
        g_reachCamera.teardownRequested.store(true, std::memory_order_release);
        g_reachCamera.armed.store(false, std::memory_order_release);
        for (int eye = 0; eye < 2; ++eye)
        {
            g_reachRenderFovSerial[eye].store(0, std::memory_order_release);
            g_reachRenderHalfFovX[eye].store(0.0f, std::memory_order_relaxed);
            g_reachRenderHalfFovY[eye].store(0.0f, std::memory_order_relaxed);
        }
        g_reachFpCameraUploadStatus.preparedSerial.store(0, std::memory_order_release);
        g_reachFpCameraUploadStatus.generation.store(0, std::memory_order_relaxed);
        g_reachFpCameraUploadStatus.eyeMask.store(0, std::memory_order_relaxed);
        g_reachFpCameraLoggedGeneration.store(0, std::memory_order_release);
        g_reachOuterCameraCommitStatus.preparedSerial.store(0, std::memory_order_release);
        g_reachOuterCameraCommitStatus.generation.store(0, std::memory_order_relaxed);
        g_reachOuterCameraCommitStatus.eyeMask.store(0, std::memory_order_relaxed);
        g_reachOuterCameraCommitLoggedGeneration.store(0, std::memory_order_release);
        // These camera-valid flags are shared with Halo 3/ODST. Reach teardown
        // can remain retryable after another title has already taken runtime
        // ownership; clearing the shared flags on every retry made Halo 3's
        // right-hand solve fall back to the stock 3DOF packet for a frame.
        // Revoke them only while Reach still owns (or no title owns) the camera.
        const GameTitle teardownOwner = TitleAdapter_GetActiveTitle();
        if (teardownOwner == GameTitle::HaloReach || teardownOwner == GameTitle::None ||
            teardownOwner == GameTitle::Unknown)
        {
            g_aimSeen.store(false, std::memory_order_release);
            g_camValid.store(false, std::memory_order_release);
            g_baseCamValid.store(false, std::memory_order_release);
        }

        // The worker has no Reach engine TLS and must never chase tag storage.
        // Active/Pending is restored only by the next proven normal-player
        // outer callback. Installing and CleanupLocked arbitrate on the same
        // atomic against a callback already admitted before this cleanup
        // request. With no later callback we deliberately retain the hook and
        // exact module reference.
        ReachSeatLeaseState seatLeaseState = ReachSeatLeaseState::Empty;
        if (!ReachAcquireNativeSeatLeaseCleanupLock(seatLeaseState))
        {
            static uint32_t loggedLeaseWaitGeneration = 0;
            const uint32_t leaseGeneration = g_reachCamera.generation.load(std::memory_order_acquire);
            if (loggedLeaseWaitGeneration != leaseGeneration)
            {
                loggedLeaseWaitGeneration = leaseGeneration;
                LOG("Reach first-person vehicles cleanup: body-hide lease "
                    "state %s requires engine-thread restoration; retaining "
                    "the outer hook and title module",
                    ReachNativeSeatLeaseStateName(seatLeaseState));
            }
            return false;
        }
        if (!DisableAndRemoveReachHooks())
            return false;

        // CleanupLocked won the same atomic race against every callback's
        // Installing claim. Only successful hook removal and quiescence make
        // it safe to forget the pointer-free bookkeeping and release the lock.
        g_reachNativeSeatLease = {};
        g_reachVehicleSeatPatchState.store(
            static_cast<uint32_t>(ReachSeatLeaseState::Empty), std::memory_order_release);
        ReachClearAimFeedback();
        ReachClearCompletedFrameFeedback();
        ReachPublishShotOccupation(0, nullptr);

        // Put the engine's third-person effect branch back before anything
        // else can unload the module. Optional, so a failure is logged rather
        // than blocking teardown.
        if (!RestoreReachThirdPersonEffectSuppression())
            LOG("Reach muzzle: third-person effect branch could not be restored");
        ReachMuzzleRetargetRestore();

        if (!RestoreReachSkyParallaxNeutralization())
        {
            LOG("Reach camera-attached sky cleanup: generic parallax quantizer "
                "could not be restored; retaining the exact title module for "
                "retry");
            return false;
        }

        if (!RestoreReachNativeWeaponIkBypass())
        {
            LOG("Reach camera cleanup: native weapon-IK disable could not be "
                "restored; retaining the exact title module for retry");
            return false;
        }

        // The eye hook normally restores this byte in its __finally. Repeat the
        // restoration after hook quiescence so even an exceptional last render
        // cannot leave the title-wide development control changed.
        if (!RestoreReachLightmapShadowsControl())
        {
            LOG("Reach world-shadow cleanup: render_lightmap_shadows could not "
                "be restored after hook quiescence; retaining the exact title "
                "module and ownership record for retry");
            return false;
        }

        const bool restoreReachBlur = g_reachCamera.motionBlurSuppressed;
        if (!ReachRestoreMotionBlurValues())
        {
            LOG("Reach camera cleanup: could not restore title-native "
                "motion-blur values; retained state will retry");
            return false;
        }
        if (restoreReachBlur)
            LOG("Reach comfort: stock motion-blur values restored during teardown");

        // A callback admitted before armed was cleared could have published
        // its final copied eye while teardown waited. Quiescence makes this
        // second invalidation definitive for the retired title generation.
        for (int eye = 0; eye < 2; ++eye)
        {
            g_reachRenderFovSerial[eye].store(0, std::memory_order_release);
            g_reachRenderHalfFovX[eye].store(0.0f, std::memory_order_relaxed);
            g_reachRenderHalfFovY[eye].store(0.0f, std::memory_order_relaxed);
        }
        const GameTitle finalTeardownOwner = TitleAdapter_GetActiveTitle();
        if (finalTeardownOwner == GameTitle::HaloReach || finalTeardownOwner == GameTitle::None ||
            finalTeardownOwner == GameTitle::Unknown)
        {
            g_aimSeen.store(false, std::memory_order_release);
            g_camValid.store(false, std::memory_order_release);
            g_baseCamValid.store(false, std::memory_order_release);
        }

        g_reachCamera.innerTarget = nullptr;
        g_reachCamera.outerTarget = nullptr;
        g_reachCamera.fpInterpolateTarget = nullptr;
        g_reachCamera.fpPaletteTarget = nullptr;
        g_reachCamera.fpCameraTarget = nullptr;
        g_reachCamera.ssaoTarget = nullptr;
        g_reachCamera.ssaoIsolationActive.store(false, std::memory_order_release);
        g_reachCamera.hudDrawWidgetTarget = nullptr;
        g_reachCamera.observerCameraTarget = nullptr;
        g_reachCamera.effectLocationTarget = nullptr;
        g_reachCamera.rainRenderTarget = nullptr;
        g_reachCamera.playerUnitByOutputUser = nullptr;
        g_reachCamera.unitInVehicle = nullptr;
        g_reachCamera.vehicleInputBindingState.store(
            static_cast<uint8_t>(ReachVehicleInputBindingState::StockFallback), std::memory_order_release);
        g_reachCamera.vehiclePlayerUnitByOutputUser = nullptr;
        g_reachCamera.unitGetCameraInfo = nullptr;
        g_reachCamera.objectMarkers = nullptr;
        g_reachCamera.objectUltimateParent = nullptr;
        g_reachCamera.vehicleType = nullptr;
        g_reachCamera.tagGet = nullptr;
        g_reachCamera.vehicleCameraBindingState.store(
            static_cast<uint8_t>(ReachVehicleCameraBindingState::StockFallback), std::memory_order_release);
        g_reachVehicleTrimSnapshot.store(0, std::memory_order_release);
        g_reachVehicleSampleMs.store(0, std::memory_order_relaxed);
        g_reachVehicleFpStable.store(static_cast<uint32_t>(Halo3VehicleState::Unknown), std::memory_order_relaxed);
        g_reachVehicleFpDebounce = {};
        g_reachSeatRecenterLatch.Clear();
        g_reachYawReferencePair.store(kReachInvalidYawReferencePair, std::memory_order_release);
        g_reachHeadReference = {};
        g_reachSeatYaw = {};
        g_reachYawSeat = {};
        // Hook removal and all module-backed restoration have succeeded. Keep
        // the retained HMODULE in a local until the remaining pointer/state clear
        // finishes, then release it exactly once before returning.
        HMODULE retiredModuleReference = g_reachCamera.moduleReference;
        g_reachCamera.moduleReference = nullptr;
        g_reachCamera.base = 0;
        g_reachCamera.size = 0;
        g_reachCamera.generation = 0;
        g_reachWorldCollision.title = GameTitle::None;
        g_reachWorldCollision.generation = 0;
        g_reachWorldCollision.fourArgumentResolver = false;
        g_reachWorldCollision.resolver = nullptr;
        g_reachWorldCollision.nextQueryAtMs.store(0, std::memory_order_release);
        g_reachWorldCollision.worker[0] = LegacyCollisionWorkerHand{};
        g_reachWorldCollision.worker[1] = LegacyCollisionWorkerHand{};
        g_reachCamera.installedAtMs = 0;
        g_reachCamera.motionBlurVars[0] = {};
        g_reachCamera.motionBlurVars[1] = {};
        g_reachCamera.motionBlurResolved = false;
        g_reachCamera.motionBlurSuppressed = false;
        g_reachCamera.decoratorWindState = nullptr;
        g_reachCamera.patchyFogFlags = nullptr;
        // render_rain is a plain module global, so a suppression can be handed
        // back from any thread once the hooks are quiescent. The atmospheric-fog
        // bit deliberately is NOT restored here: it lives in per-thread render
        // TLS, so only the render thread can reach the right copy, and teardown
        // does not run there. Turning the setting back on restores it on the
        // next eye, and a title-module reload re-initialises the byte anyway.
        if (g_reachCamera.renderRainSuppressed && g_reachCamera.renderRainSlot)
        {
            SafeWriteByte(g_reachCamera.renderRainSlot, g_reachCamera.renderRainOriginal);
        }
        g_reachCamera.renderRainSlot = nullptr;
        g_reachCamera.renderRainOriginal = 1;
        g_reachCamera.renderRainSuppressed = false;
        g_reachCamera.atmosphereFogTlsIndex = nullptr;
        g_reachCamera.atmosphereFogSuppressed = false;
        g_reachCamera.lightmapShadowsEnabled = nullptr;
        g_reachLightmapShadowOutstandingValue.store(0, std::memory_order_relaxed);
        g_reachLightmapShadowRestoreOutstanding.store(false, std::memory_order_release);
        g_reachCamera.nativeWeaponIkDisable = nullptr;
        g_reachCamera.nativeWeaponIkDisableOriginal = 0;
        g_reachCamera.nativeWeaponIkBypassActive = false;
        g_reachOrigPlayerViewRender = nullptr;
        g_reachOrigMainRenderView = nullptr;
        g_reachOrigFpInterpolate = nullptr;
        g_reachOrigFpPalette = nullptr;
        g_reachOrigFpCameraRebuild = nullptr;
        g_reachFpCameraUpload = nullptr;
        g_reachOrigRenderSsao = nullptr;
        g_reachOrigHudDrawWidget = nullptr;
        g_reachFpStatus.key.store(0, std::memory_order_release);
        g_reachFpLoggedStatusKey.store(0, std::memory_order_release);
        g_reachFpLegPaletteObservationCount.store(0, std::memory_order_relaxed);
        g_reachFpLegPalettePreserved.store(0, std::memory_order_relaxed);
        g_reachFpLegPaletteIdentityRejects.store(0, std::memory_order_relaxed);
        g_reachFpLegPaletteBuildFailures.store(0, std::memory_order_relaxed);
        g_reachFpCameraUploadStatus.preparedSerial.store(0, std::memory_order_release);
        g_reachFpCameraUploadStatus.generation.store(0, std::memory_order_relaxed);
        g_reachFpCameraUploadStatus.eyeMask.store(0, std::memory_order_relaxed);
        g_reachFpCameraLoggedGeneration.store(0, std::memory_order_release);
        g_reachOuterCameraCommitStatus.preparedSerial.store(0, std::memory_order_release);
        g_reachOuterCameraCommitStatus.generation.store(0, std::memory_order_relaxed);
        g_reachOuterCameraCommitStatus.eyeMask.store(0, std::memory_order_relaxed);
        g_reachOuterCameraCommitLoggedGeneration.store(0, std::memory_order_release);
        g_reachHelpers = {};
        g_reachCamera.teardownRequested.store(false, std::memory_order_release);
        g_reachCamera.installed.store(false, std::memory_order_release);
        // The next install must prove the level is running again.
        g_reachLevelLoadGate.Rearm();
        if (retiredModuleReference)
            FreeLibrary(retiredModuleReference);
        LOG("Reach camera core removed; stock Reach owns the title");
        return true;
    }

    bool ReachColdExecutableAddress(uintptr_t address)
    {
        return sig::IsCodeAddress(address); // one shared check
    }
    void* ResolveReachDecoratorWind(uintptr_t base, size_t size)
    {
        // HREK 0x831790 -> 0x2AAA50; retail decorator draw -> wind update.
        // Verify unique update, accumulator access, call site and shader uploads.
        if (size < 0xD1F180 ||
            !ReachColdExactSignatureAt(base, size, 0x77EC2C,
                "48 8B C4 48 89 58 08 57 48 81 EC A0 00 00 00 0F 29 70 E8 48 8B F9 0F 29 78 D8 44 0F 29 40 C8 44 0F 29 48 B8") ||
            !ReachColdExactSignatureAt(base, size, 0x77EC8A,
                "66 0F 6E 05 C2 04 5A 00 41 0F 28 CD F3 0F 59 0D 6A C9 30 00 0F 5B C0 F3 0F 58 C8 F3 0F 2C C1 89 05 A5 04 5A 00") ||
            !ReachColdExactSignatureAt(base, size, 0x2A0C41,
                "48 85 DB 74 17 48 8B 05 5B 7D B9 04 39 B0 90 03 00 00 75 08 48 8B CB E8 CF DF 4D 00") ||
            !ReachColdExactSignatureAt(base, size, 0x77EBF7,
                "41 B8 10 00 00 00 48 8D 15 5C 05 5A 00 B9 00 00 2C 00 E8 F2 25 AF FF 41 B8 10 00 00 00 48 8D 15 55 05 5A 00 B9 01 00 2C 00"))
            return nullptr;
        MEMORY_BASIC_INFORMATION memory{};
        void* state = reinterpret_cast<void*>(base + 0xD1F150);
        if (!VirtualQuery(state, &memory, sizeof(memory)) || memory.State != MEM_COMMIT ||
            (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) || !(memory.Protect & (PAGE_READWRITE | PAGE_WRITECOPY)) ||
            reinterpret_cast<uintptr_t>(memory.BaseAddress) + memory.RegionSize < base + 0xD1F180)
            return nullptr;
        return state;
    }

    // Optional shot-line feature only. HREK proves the unit
    // camera-position evaluator (HREK 0xD76E60, consumed by pinned
    // unit_get_camera_info) and its two firing consumers: the unit-adjust
    // body (retail homolog 0x484F24, whole-body SHA-256 pinned)
    // calling the evaluator inside a mode-6 head-helper/else
    // block, and the projectile transaction's direct clip-anchor call. Both
    // return addresses are derived at runtime from their unique call-site
    // blocks by decoding the E8 rel32 edges and requiring them to target the
    // AOB-verified evaluator - never hardcoded. Failure keeps only this
    // feature stock; the camera core is untouched.
    bool ResolveReachFiringOriginBinding(
        uintptr_t base, size_t size, uintptr_t& outEvaluator, uintptr_t& outReturnA, uintptr_t& outReturnB)
    {
        outEvaluator = 0;
        outReturnA = 0;
        outReturnB = 0;
        static constexpr char kEvaluatorAob[] =
            "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 41 54 41 55 41 56 41 57 48 8D 68 A1 48 81 EC B0 00 00 00 44 8B 05 ?? ?? ?? ?? 41 83 CF FF 65 48 8B 04 25 58 00 00 00";
        // 83 3C 30 06 (anim mode == 6?) / 75 07 / E8 head-helper / EB 05 /
        // E8 evaluator / 80 7D 77 00. The SECOND call is the shot line.
        static constexpr char kAdjustCallBlockAob[] =
            "83 3C 30 06 75 07 E8 ?? ?? ?? ?? EB 05 E8 ?? ?? ?? ?? 80 7D 77 00";
        // The projectile transaction's guarded direct call: E8 evaluator then
        // E8 object_get_ultimate_parent. The FIRST call is the shot line.
        static constexpr char kDirectCallBlockAob[] =
            "8B 5C 24 68 39 7C 24 7C 7F 0E 48 8D 95 A8 01 00 00 8B CB E8 ?? ?? ?? ?? 8B CB E8 ?? ?? ?? ?? 44 8B C8";
        constexpr uintptr_t kEvaluatorRva = 0x48A1C0;
        constexpr uintptr_t kAdjustBlockRva = 0x484FCF;
        constexpr uintptr_t kDirectBlockRva = 0x4C30F4;
        constexpr uintptr_t kAdjustCallOffset = 13;
        constexpr uintptr_t kDirectCallOffset = 19;
        if (!ReachColdExactSignatureAt(base, size, kEvaluatorRva, kEvaluatorAob) ||
            !ReachColdExactSignatureAt(base, size, kAdjustBlockRva, kAdjustCallBlockAob) ||
            !ReachColdExactSignatureAt(base, size, kDirectBlockRva, kDirectCallBlockAob))
            return false;
        const auto rel32Target = [](uintptr_t call)
        {
            int32_t rel = 0;
            memcpy(&rel, reinterpret_cast<const void*>(call + 1), sizeof(rel));
            return static_cast<uintptr_t>(static_cast<intptr_t>(call + 5) + rel);
        };
        const uintptr_t evaluator = base + kEvaluatorRva;
        const uintptr_t adjustCall = base + kAdjustBlockRva + kAdjustCallOffset;
        const uintptr_t directCall = base + kDirectBlockRva + kDirectCallOffset;
        if (rel32Target(adjustCall) != evaluator || rel32Target(directCall) != evaluator)
            return false;
        outEvaluator = evaluator;
        outReturnA = adjustCall + 5;
        outReturnB = directCall + 5;
        return true;
    }

    // HREK weapons.cpp: unit-adjust (0xD67EE0) is called exactly once by the
    // projectile transaction after it resolves the weapon owner, active barrel
    // and deepest controlling unit. The pinned retail homolog is 0x484F24 and
    // its sole projectile call is 0x4C303A. Retail is used only to match those
    // already-understood HREK constructs.
    bool ResolveReachUnitAdjustBinding(uintptr_t base, size_t size, uintptr_t& outUnitAdjust)
    {
        outUnitAdjust = 0;
        static constexpr char kUnitAdjustAob[] = "48 8B C4 55 53 56 57 41 56 41 57 48 8D 68 C9 "
                                                 "48 81 EC D8 00 00 00 0F 29 70 B8 0F 29 78 A8 "
                                                 "44 0F 29 40 98 44 0F 29 48 88";
        constexpr uintptr_t kUnitAdjustRva = 0x00484F24;
        constexpr uintptr_t kProjectileCallRva = 0x004C303A;
        if (!ReachColdExactSignatureAt(base, size, kUnitAdjustRva, kUnitAdjustAob) ||
            !ReachVerifyRel32Call(base, kProjectileCallRva, kUnitAdjustRva))
        {
            return false;
        }
        outUnitAdjust = base + kUnitAdjustRva;
        return true;
    }

    // Official HREK render_first_person_view.cpp 0x8362F0 tests the result of
    // HREK 0x1E6960 at 0x83639D and skips the complete first-person render when
    // it is not NONE.  The pinned optimized retail homolog is 0x26EA78; its
    // single corresponding call is 0x26EAD9 -> 0x6527C.  Retail is used only to
    // match this HREK-understood render boundary.
    bool ResolveReachFirstPersonRenderGateBinding(
        uintptr_t base, size_t size, uintptr_t& outGate, uintptr_t& outRenderReturn)
    {
        outGate = 0;
        outRenderReturn = 0;
        static constexpr char kGateAob[] = "83 C8 FF 3B C8 74 2F 65 48 8B 04 25 58 00 00 00 "
                                           "8B 15 ?? ?? ?? ?? 41 B9 D8 00 00 00 4C 8B 04 D0 "
                                           "48 63 C1 48 69 C8 B0 00 00 00 4B 8B 04 08 "
                                           "0F B7 84 08 C6 06 00 00 C3";
        static constexpr char kRenderAob[] = "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 "
                                             "4C 89 60 20 41 56 48 83 EC 30 41 BE 01 00 00 00 "
                                             "40 8A F2 41 8A DE 48 8B F9";
        constexpr uintptr_t kGateRva = 0x0006527C;
        constexpr uintptr_t kRenderRva = 0x0026EA78;
        constexpr uintptr_t kGateCallRva = 0x0026EAD9;
        if (!ReachColdExactSignatureAt(base, size, kGateRva, kGateAob) ||
            !ReachColdExactSignatureAt(base, size, kRenderRva, kRenderAob) ||
            !ReachVerifyRel32Call(base, kGateCallRva, kGateRva))
        {
            return false;
        }
        outGate = base + kGateRva;
        outRenderReturn = base + kGateCallRva + 5;
        return true;
    }

    // Optional input feature only. HREK supplies the semantics and ABIs; retail
    // is used only to match and verify the known homologs. Failure preserves the
    // existing Reach-wide LT/X swap and cannot gate the camera core.
    bool ResolveReachVehicleInputBinding(uintptr_t base, size_t size,
        ReachPlayerUnitByOutputUserFn& playerUnitByOutputUser, ReachUnitInVehicleFn& unitInVehicle)
    {
        playerUnitByOutputUser = nullptr;
        unitInVehicle = nullptr;
        static constexpr char kPlayerUnitAob[] =
            "83 C8 FF 83 F9 03 77 27 8B 15 12 3C BC 00 65 48 8B 04 25 58 00 00 00 41 B8 58 01 00 00 48 63 C9 48 8B 04 D0 4A 8B 04 00 8B 84 88 C8 00 00 00 C3";
        static constexpr char kUnitInVehicleAob[] =
            "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 45 32 DB 41 83 C9 FF 41 3B C9 0F 84 80 00 00 00 8B 15 8B E7 71 00 65 48 8B 04 25 58 00 00 00 41 B8 10 00 00 00";
        static constexpr char kEvaluatorAob[] =
            "40 53 48 83 EC 20 48 83 64 24 48 00 8B DA 48 0F BF C1 45 8A C8 48 8D 0D FC 9D 88 00 48 8B 0C C1 0F B7 51 38 4C 8D 41 3A 8B CB E8 91 76 EE FF 48 85 C0 74 16 8B 08 E8 05 A4 35 00";
        static constexpr char kName[] = "unit_in_vehicle";
        const auto fits = [size](uintptr_t rva, size_t bytes) { return rva < size && bytes <= size - rva; };
        if (!fits(kReachPlayerUnitByOutputUserRva, 49) || !fits(kReachUnitInVehicleRva, 53) ||
            !fits(kReachUnitInVehicleEvaluatorRva, 57) || !fits(kReachUnitInVehicleEvaluatorCallRva, 5) ||
            !fits(kReachUnitInVehicleNameRva, sizeof(kName)) || !fits(kReachUnitInVehicleDescriptorRva, 40) ||
            !ReachColdExactSignatureAt(base, size, kReachPlayerUnitByOutputUserRva, kPlayerUnitAob) ||
            !ReachColdExactSignatureAt(base, size, kReachUnitInVehicleRva, kUnitInVehicleAob) ||
            !ReachColdExactSignatureAt(base, size, kReachUnitInVehicleEvaluatorRva, kEvaluatorAob) ||
            !ReachVerifyRel32Call(base, kReachUnitInVehicleEvaluatorCallRva, kReachUnitInVehicleRva) ||
            memcmp(reinterpret_cast<const void*>(base + kReachUnitInVehicleNameRva), kName, sizeof(kName)) != 0)
            return false;

        const auto tlsLoadMatches = [base](uintptr_t rva)
        {
            const auto* p = reinterpret_cast<const uint8_t*>(base + rva);
            if (p[0] != 0x8B || p[1] != 0x15)
                return false;
            int32_t displacement = 0;
            memcpy(&displacement, p + 2, sizeof(displacement));
            return static_cast<uintptr_t>(static_cast<intptr_t>(base + rva + 6) + displacement) ==
                   base + kReachEngineTlsIndexRva;
        };
        if (!tlsLoadMatches(kReachPlayerUnitByOutputUserRva + 8) || !tlsLoadMatches(kReachUnitInVehicleRva + 31))
            return false;

        const auto* descriptor = reinterpret_cast<const uint64_t*>(base + kReachUnitInVehicleDescriptorRva);
        if (descriptor[0] != 5 || descriptor[1] != base + kReachUnitInVehicleNameRva ||
            descriptor[4] != base + kReachUnitInVehicleEvaluatorRva)
            return false;

        playerUnitByOutputUser =
            reinterpret_cast<ReachPlayerUnitByOutputUserFn>(base + kReachPlayerUnitByOutputUserRva);
        unitInVehicle = reinterpret_cast<ReachUnitInVehicleFn>(base + kReachUnitInVehicleRva);
        return ReachColdExecutableAddress(reinterpret_cast<uintptr_t>(playerUnitByOutputUser)) &&
               ReachColdExecutableAddress(reinterpret_cast<uintptr_t>(unitInVehicle));
    }

    // Optional Reach first-person vehicle transaction. HREK supplies every ABI
    // and layout; these unique retail entry signatures only locate and verify
    // the homologs in the already full-image-pinned module. Failure affects no
    // other Reach camera feature.
    bool ResolveReachVehicleCameraBinding(uintptr_t base, size_t size,
        ReachPlayerUnitByOutputUserFn& playerUnitByOutputUser, ReachUnitGetCameraInfoFn& unitGetCameraInfo,
        ReachObjectMarkersFn& objectMarkers, ReachObjectUltimateParentFn& objectUltimateParent,
        ReachVehicleTypeFn& vehicleType, ReachTagGetFn& tagGet)
    {
        playerUnitByOutputUser = nullptr;
        unitGetCameraInfo = nullptr;
        objectMarkers = nullptr;
        objectUltimateParent = nullptr;
        vehicleType = nullptr;
        tagGet = nullptr;
        static constexpr char kPlayerUnitAob[] =
            "83 C8 FF 83 F9 03 77 27 8B 15 12 3C BC 00 65 48 8B 04 25 58 00 00 00 41 B8 58 01 00 00 48 63 C9 48 8B 04 D0 4A 8B 04 00 8B 84 88 C8 00 00 00 C3";
        static constexpr char kCameraInfoAob[] =
            "48 8B C4 48 89 58 18 55 56 57 41 54 41 55 41 56 41 57 48 8D 68 A1 48 81 EC E0 00 00 00 41 83 CE FF 8B D9 33 FF 0F 29 70 B8 48 89 7A 08";
        static constexpr char kObjectMarkersAob[] =
            "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 66 44 89 48 20 57 41 54 41 55 41 56 41 57 48 81 EC 80 00 00 00 45 33 D2 8B E9 49 8B F8 44 8B F2 8B D9 41 0F B7 F2";
        static constexpr char kUltimateParentAob[] =
            "83 C8 FF 3B C8 74 37 8B 15 ?? ?? ?? ?? 65 48 8B 04 25 58 00 00 00 41 B8 10 00 00 00 48 8B 04 D0 4A 8B 14 00 4C 8B 42 50 0F B7 D1 8B C1 48 8D 0C 52 49 8B 54 C8 10 8B 4A 14 83 F9 FF 75 EA C3";
        static constexpr char kVehicleTypeAob[] =
            "48 8B 05 ?? ?? ?? ?? 0F B7 C9 8B 54 C8 04 48 8D 0D ?? ?? ?? ?? 8B C2 48 C1 E8 1C 4C 8D 82 34 01 00 00 48 8B 0C C1 33 C0 45 33 C9 44 8D 50 10 4E 8D 04 81 41 83 38 00 75 11 FF C0 49 FF C1 49 83 C0 0C 49 83 F9 0F 7C EB EB 03 44 8B D0 41 8B C2 C3";
        static constexpr char kTagGetAob[] =
            "4C 8B 15 ?? ?? ?? ?? 45 33 DB 4C 0F BF C2 45 8B CB 49 63 82 F8 FF 03 00 4C 3B C0 73 17 48 8B 05 ?? ?? ?? ?? C1 FA 10 4E 8D 04 C0 66 41 3B 50 02 4D 0F 44 C8 4D 85 C9 74 3E 49 0F BF 01 48 03 C0 41 39 8C C2 FC FF 03 00 74 14 41 39 8C C2 00 00 04 00 74 0A 41 39 8C C2 04 00 04 00 75 19 41 8B 49 04 48 8D 15 ?? ?? ?? ?? 8B C1 48 C1 E8 1C 48 8B 04 C2 4C 8D 1C 88 49 8B C3 C3";
        if (!ReachColdExactSignatureAt(base, size, kReachPlayerUnitByOutputUserRva, kPlayerUnitAob) ||
            !ReachColdExactSignatureAt(base, size, kReachUnitGetCameraInfoRva, kCameraInfoAob) ||
            !ReachColdExactSignatureAt(base, size, kReachObjectMarkerResolverRva, kObjectMarkersAob) ||
            !ReachColdExactSignatureAt(base, size, kReachVehicleTypeAccessorRva, kVehicleTypeAob) ||
            !ReachColdExactSignatureAt(base, size, kReachTagGetRva, kTagGetAob))
        {
            return false;
        }
        playerUnitByOutputUser =
            reinterpret_cast<ReachPlayerUnitByOutputUserFn>(base + kReachPlayerUnitByOutputUserRva);
        unitGetCameraInfo = reinterpret_cast<ReachUnitGetCameraInfoFn>(base + kReachUnitGetCameraInfoRva);
        objectMarkers = reinterpret_cast<ReachObjectMarkersFn>(base + kReachObjectMarkerResolverRva);
        if (ReachColdExactSignatureAt(base, size, kReachObjectUltimateParentRva, kUltimateParentAob))
        {
            objectUltimateParent = reinterpret_cast<ReachObjectUltimateParentFn>(base + kReachObjectUltimateParentRva);
        }
        vehicleType = reinterpret_cast<ReachVehicleTypeFn>(base + kReachVehicleTypeAccessorRva);
        tagGet = reinterpret_cast<ReachTagGetFn>(base + kReachTagGetRva);
        return ReachColdExecutableAddress(reinterpret_cast<uintptr_t>(playerUnitByOutputUser)) &&
               ReachColdExecutableAddress(reinterpret_cast<uintptr_t>(unitGetCameraInfo)) &&
               ReachColdExecutableAddress(reinterpret_cast<uintptr_t>(objectMarkers)) &&
               ReachColdExecutableAddress(reinterpret_cast<uintptr_t>(vehicleType)) &&
               ReachColdExecutableAddress(reinterpret_cast<uintptr_t>(tagGet));
    }

    bool ResolveReachNativeWeaponIkControl(uintptr_t base, size_t size, uint8_t*& slot, uint8_t& original)
    {
        slot = nullptr;
        original = 0;
        static constexpr char kDecisionAob[] = "41 0F B7 86 2C 53 00 00 66 85 C0 0F 8E 52 02 00 00 "
                                               "38 1D DC 3A B8 04 0F 85 46 02 00 00 48 8B 15 C6 88 "
                                               "99 00 0F BF C8";
        if (!ReachColdExactSignatureAt(base, size, kReachFpWeaponIkDecisionPreludeRva, kDecisionAob) ||
            kReachFpWeaponIkDisableEntryRva >= size || 24 > size - kReachFpWeaponIkDisableEntryRva)
        {
            return false;
        }

        const auto* entry = reinterpret_cast<const uint8_t*>(base + kReachFpWeaponIkDisableEntryRva);
        uintptr_t entryName = 0;
        uint64_t entryType = 0;
        memcpy(&entryName, entry, sizeof(entryName));
        memcpy(&entryType, entry + 8, sizeof(entryType));
        if (entryName != base + kReachFpWeaponIkDisableNameRva || entryType != kReachDebugBooleanType)
        {
            return false;
        }

        const auto* compare = reinterpret_cast<const uint8_t*>(base + kReachFpWeaponIkDisableCompareRva);
        const auto* branch = reinterpret_cast<const uint8_t*>(base + kReachFpWeaponIkDisableBranchRva);
        if (compare[0] != 0x38 || compare[1] != 0x1D || branch[0] != 0x0F || branch[1] != 0x85)
        {
            return false;
        }
        int32_t flagDisplacement = 0;
        int32_t branchDisplacement = 0;
        memcpy(&flagDisplacement, compare + 2, sizeof(flagDisplacement));
        memcpy(&branchDisplacement, branch + 2, sizeof(branchDisplacement));
        const uintptr_t flagTarget = static_cast<uintptr_t>(
            static_cast<intptr_t>(base + kReachFpWeaponIkDisableCompareRva + 6) + flagDisplacement);
        const uintptr_t disabledTarget = static_cast<uintptr_t>(
            static_cast<intptr_t>(base + kReachFpWeaponIkDisableBranchRva + 6) + branchDisplacement);
        // HREK publishes a value pointer in its development debug descriptor;
        // retail leaves that descriptor field unpublished. The shipping
        // consumer instruction is the authoritative binding: decode its exact
        // RIP-relative byte and require the pinned Reach-specific target.
        uint8_t* const resolved = reinterpret_cast<uint8_t*>(flagTarget);
        if (flagTarget != base + kReachFpWeaponIkDisableValueRva ||
            disabledTarget != base + kReachFpWeaponIkDisabledEpilogueRva ||
            !ReachColdExecutableAddress(disabledTarget) || !SafeReadByte(resolved, &original) || original > 1)
        {
            return false;
        }
        slot = resolved;
        return true;
    }

    // ---- selective muzzle retarget: put the odd FP particle on the gun -----
    // See kReachEffectHandleTableRva in reach_render_logic.h. The hot hook
    // captures the definition index of the player's own weapon effects; this
    // worker decodes the loaded tag through the engine's own handle/pool
    // arithmetic, finds the single first-person system whose location differs
    // from its siblings, and writes the siblings' location index over it. The
    // siblings provably track the controller-held gun.
    constinit const char* kReachEffectDecodeSig = "4C 8B 2D ?? ?? ?? ?? 4C 8D 0D ?? ?? ?? ?? 41 BC 48 00 00 00";
    constinit uintptr_t g_reachMuzzleHandleTable = 0; // decoded from the signature
    constinit uintptr_t g_reachMuzzlePoolTable = 0;
    constinit bool g_reachMuzzleDecodeResolved = false;
    struct ReachMuzzlePatch
    {
        uintptr_t address;
        uint16_t original;
    };
    constinit ReachMuzzlePatch g_reachMuzzlePatches[32]{};
    constinit size_t g_reachMuzzlePatchCount = 0;
    constinit uint32_t g_reachMuzzleAttemptedDefs[16]{};
    constinit size_t g_reachMuzzleAttemptedCount = 0;
    constinit std::atomic<uint32_t> g_reachMuzzleRetargetDone{0};

    bool ReachReadU32(uintptr_t address, uint32_t* out)
    {
        __try
        {
            *out = *reinterpret_cast<const uint32_t*>(address);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading a Reach u32 value faulted");
            return false;
        }
    }
    bool ReachReadU16(uintptr_t address, uint16_t* out)
    {
        __try
        {
            *out = *reinterpret_cast<const uint16_t*>(address);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading a Reach u16 value faulted");
            return false;
        }
    }
    bool ReachWriteU16(uintptr_t address, uint16_t value)
    {
        __try
        {
            *reinterpret_cast<uint16_t*>(address) = value;
            return *reinterpret_cast<const uint16_t*>(address) == value;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("writing a Reach u16 value faulted");
            return false;
        }
    }

    // Decode pool[handle >> 28] + handle*4, the engine's own arithmetic.
    bool ReachDecodePoolAddress(uint32_t handle, uintptr_t* out)
    {
        uintptr_t poolBase = 0;
        __try
        {
            poolBase =
                *reinterpret_cast<const uintptr_t*>(g_reachMuzzlePoolTable + static_cast<uintptr_t>(handle >> 28) * 8);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("decoding a Reach pool address faulted");
            return false;
        }
        if (!poolBase)
            return false;
        *out = poolBase + static_cast<uintptr_t>(handle) * 4;
        return true;
    }

    void ReachMuzzleRetargetTick(uintptr_t base, size_t size, uint32_t generation, bool soleReachTitle)
    {
        static uint32_t attemptedGeneration = 0;
        if (!soleReachTitle || !base || !generation)
        {
            g_reachMuzzlePatchCount = 0;
            g_reachMuzzleAttemptedCount = 0;
            g_reachMuzzleDecodeResolved = false;
            g_reachMuzzleRetargetDone.store(0, std::memory_order_relaxed);
            g_reachMuzzleCapturedDefIndex.store(0xFFFFFFFFu, std::memory_order_relaxed);
            attemptedGeneration = 0;
            return;
        }
        if (attemptedGeneration != generation)
        {
            attemptedGeneration = generation;
            g_reachMuzzlePatchCount = 0;
            g_reachMuzzleAttemptedCount = 0;
            g_reachMuzzleDecodeResolved = false;
            g_reachMuzzleRetargetDone.store(0, std::memory_order_relaxed);
        }
        if (!g_reachMuzzleDecodeResolved)
        {
            const uintptr_t hit = sig::Find(base, size, kReachEffectDecodeSig);
            if (!hit || sig::Find(hit + 1, base + size - hit - 1, kReachEffectDecodeSig))
            {
                return; // silent; retried next tick, logged via REACHFX
            }
            const uintptr_t tableSlot = sig::RipTarget(hit + 3, hit + 7);
            const uintptr_t poolTable = sig::RipTarget(hit + 10, hit + 14);
            if (tableSlot < base || tableSlot >= base + size || poolTable < base || poolTable >= base + size)
            {
                return;
            }
            __try
            {
                g_reachMuzzleHandleTable = *reinterpret_cast<const uintptr_t*>(tableSlot);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                GUARD_COUNT("reading the Reach muzzle handle table faulted");
                return;
            }
            if (!g_reachMuzzleHandleTable)
                return;
            g_reachMuzzlePoolTable = poolTable;
            g_reachMuzzleDecodeResolved = true;
            LOG("Reach muzzle: tag decode resolved (handle table slot "
                "haloreach.dll+0x%llX, pool haloreach.dll+0x%llX)",
                (unsigned long long)(tableSlot - base), (unsigned long long)(poolTable - base));
        }
        const uint32_t defIndex = g_reachMuzzleCapturedDefIndex.load(std::memory_order_relaxed);
        if (defIndex == 0xFFFFFFFFu)
            return;
        // One attempt per definition; the player swaps weapons, so several
        // definitions accumulate over a session (six weapons qualify).
        for (size_t i = 0; i < g_reachMuzzleAttemptedCount; ++i)
            if (g_reachMuzzleAttemptedDefs[i] == defIndex)
                return;
        if (g_reachMuzzleAttemptedCount >= _countof(g_reachMuzzleAttemptedDefs))
        {
            return;
        }
        g_reachMuzzleAttemptedDefs[g_reachMuzzleAttemptedCount++] = defIndex;

        uint32_t handle = 0;
        if (!ReachReadU32(g_reachMuzzleHandleTable + static_cast<uintptr_t>(defIndex & 0xFFFFu) * 8 + 4, &handle) ||
            handle == 0 || handle == 0xFFFFFFFFu)
        {
            return;
        }
        uintptr_t defBase = 0;
        if (!ReachDecodePoolAddress(handle, &defBase))
            return;
        uint32_t eventsHandle = 0;
        if (!ReachReadU32(defBase + kReachEffectDefEventsBlockOffset, &eventsHandle) || !eventsHandle ||
            eventsHandle == 0xFFFFFFFFu)
        {
            return;
        }
        uintptr_t eventsBase = 0;
        if (!ReachDecodePoolAddress(eventsHandle, &eventsBase))
            return;

        for (size_t eventIndex = 0; eventIndex < 8; ++eventIndex)
        {
            const uintptr_t eventElement = eventsBase + eventIndex * kReachEffectEventStride;
            uint32_t psysHandle = 0;
            if (!ReachReadU32(eventElement + kReachEffectEventPsysBlockOffset, &psysHandle) || !psysHandle ||
                psysHandle == 0xFFFFFFFFu)
            {
                continue;
            }
            uintptr_t psysBase = 0;
            if (!ReachDecodePoolAddress(psysHandle, &psysBase))
                continue;
            unsigned short modes[kReachEffectMaxWalk]{};
            unsigned short locations[kReachEffectMaxWalk]{};
            size_t count = 0;
            for (; count < kReachEffectMaxWalk; ++count)
            {
                uint16_t mode = 0;
                uint16_t location = 0;
                if (!ReachReadU16(
                        psysBase + count * kReachEffectPsysStride + kReachEffectPsysCameraModeOffset, &mode) ||
                    !ReachReadU16(
                        psysBase + count * kReachEffectPsysStride + kReachEffectPsysLocationOffset, &location) ||
                    mode > 3 || location > 15)
                {
                    break;
                }
                modes[count] = mode;
                locations[count] = location;
            }
            const ReachMuzzleRetargetPlan plan = ReachDecideMuzzleRetarget(modes, locations, count);
            if (!plan.count)
                continue;
            unsigned applied = 0;
            for (int e = 0; e < plan.count; ++e)
            {
                if (g_reachMuzzlePatchCount >= _countof(g_reachMuzzlePatches))
                {
                    break;
                }
                const int element = plan.elements[e];
                const uintptr_t target =
                    psysBase + static_cast<size_t>(element) * kReachEffectPsysStride + kReachEffectPsysLocationOffset;
                const uint16_t original = locations[element];
                if (!ReachWriteU16(target, plan.newLocation))
                    continue;
                g_reachMuzzlePatches[g_reachMuzzlePatchCount++] = {target, original};
                ++applied;
            }
            if (applied)
            {
                g_reachMuzzleRetargetDone.fetch_add(applied, std::memory_order_relaxed);
                LOG("Reach muzzle: RETARGETED %u first-person particle "
                    "system(s) of definition %u event %zu onto location %u "
                    "(the marker their siblings track the gun with)",
                    applied, defIndex & 0xFFFFu, eventIndex, plan.newLocation);
            }
            // keep scanning further events of this definition
        }
    }

    void ReachMuzzleRetargetRestore()
    {
        for (size_t i = 0; i < g_reachMuzzlePatchCount; ++i)
            ReachWriteU16(g_reachMuzzlePatches[i].address, g_reachMuzzlePatches[i].original);
        g_reachMuzzlePatchCount = 0;
        g_reachMuzzleAttemptedCount = 0;
        g_reachMuzzleRetargetDone.store(0, std::memory_order_relaxed);
        g_reachMuzzleCapturedDefIndex.store(0xFFFFFFFFu, std::memory_order_relaxed);
    }

    // ---- suppress the third-person muzzle flash on the player's own body ----
    // See kReachThirdPersonEffectDenyRva for the derivation. Reach's effect
    // render pass allows a "third person only" particle system whenever the
    // effect is not flagged as the player's own first-person weapon - and the
    // player's own WORLD weapon is not flagged, because flat Reach never shows
    // it. VR does, so it renders at the body.
    //
    // This takes the engine's own deny path for camera-mode-2 systems. Applied
    // when the Reach core arms and restored on teardown, exactly like the
    // native weapon-IK bypass above.
    constinit const char* kReachThirdPersonEffectSig = "F9 41 B2 01 85 C9 74 1E 83 E9 01 74 14 83 F9 01 75 17 "
                                                       "45 85 F6 75 05 45 85 C0 74 0D 45 32 D2";
    constinit uintptr_t g_reachThirdPersonEffectPatchSite = 0;
    constinit bool g_reachThirdPersonEffectPatched = false;
    constinit uintptr_t g_reachSkyParallaxPatchSite = 0;
    constinit bool g_reachSkyParallaxPatched = false;

    bool ReachWriteCode(uintptr_t address, const unsigned char* bytes, size_t count)
    {
        DWORD previous = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(address), count, PAGE_EXECUTE_READWRITE, &previous))
        {
            return false;
        }
        bool ok = true;
        __try
        {
            memcpy(reinterpret_cast<void*>(address), bytes, count);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("writing Reach code bytes faulted");
            ok = false;
        }
        DWORD restored = 0;
        VirtualProtect(reinterpret_cast<void*>(address), count, previous, &restored);
        if (ok)
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), count);
        return ok;
    }

    bool ApplyReachSkyParallaxNeutralization(uintptr_t base, size_t size)
    {
        static constexpr char kSkyParallaxConversionAob[] = "A8 40 74 24 66 83 4B 04 40 F3 41 0F 10 84 90 B4 01 00 00 "
                                                            "F3 0F 59 C6 F3 0F 2C C0 88 43 0B 41";
        g_reachSkyParallaxPatchSite = 0;
        g_reachSkyParallaxPatched = false;
        if (!ReachColdExactSignatureAt(base, size, kReachSkyParallaxSignatureRva, kSkyParallaxConversionAob))
        {
            LOG("Reach camera-attached sky: exact generic model-to-object "
                "parallax conversion proof failed; authored stock parallax "
                "remains active, nothing else affected");
            return false;
        }

        const uintptr_t site = base + kReachSkyParallaxQuantizeRva;
        if (kReachSkyParallaxQuantizeRva >= size ||
            kReachSkyParallaxQuantizeOriginal.size() > size - kReachSkyParallaxQuantizeRva ||
            memcmp(reinterpret_cast<const void*>(site), kReachSkyParallaxQuantizeOriginal.data(),
                kReachSkyParallaxQuantizeOriginal.size()) != 0)
        {
            LOG("Reach camera-attached sky: quantizer does not hold the exact "
                "HREK-homologous bytes; authored stock parallax remains active");
            return false;
        }
        if (!ReachWriteCode(site, kReachSkyParallaxQuantizeNeutral.data(), kReachSkyParallaxQuantizeNeutral.size()))
        {
            LOG("Reach camera-attached sky: could not neutralize the generic "
                "parallax quantizer; authored stock parallax remains active");
            return false;
        }

        g_reachSkyParallaxPatchSite = site;
        g_reachSkyParallaxPatched = true;
        LOG("Reach camera-attached sky: generic model flag 0x%X at +0x%llX "
            "kept, authored parallax +0x%llX neutralized at "
            "haloreach.dll+0x%llX for zero binocular disparity; ordinary "
            "world skies are untouched",
            static_cast<unsigned>(kReachModelAttachToCameraMask),
            static_cast<unsigned long long>(kReachModelFlagsOffset),
            static_cast<unsigned long long>(kReachModelSkyParallaxOffset),
            static_cast<unsigned long long>(kReachSkyParallaxQuantizeRva));
        return true;
    }

    bool RestoreReachSkyParallaxNeutralization()
    {
        if (!g_reachSkyParallaxPatched || !g_reachSkyParallaxPatchSite)
            return true;
        const bool ok = ReachWriteCode(g_reachSkyParallaxPatchSite, kReachSkyParallaxQuantizeOriginal.data(),
            kReachSkyParallaxQuantizeOriginal.size());
        if (ok)
        {
            g_reachSkyParallaxPatched = false;
            g_reachSkyParallaxPatchSite = 0;
        }
        return ok;
    }

    bool ApplyReachThirdPersonEffectSuppression(uintptr_t base, size_t size)
    {
        g_reachThirdPersonEffectPatchSite = 0;
        g_reachThirdPersonEffectPatched = false;
        const uintptr_t hit = sig::Find(base, size, kReachThirdPersonEffectSig);
        if (!hit || sig::Find(hit + 1, base + size - hit - 1, kReachThirdPersonEffectSig))
        {
            LOG("Reach muzzle: third-person effect signature %s; the "
                "body-anchored muzzle flash stays, nothing else affected",
                hit ? "ambiguous" : "missing");
            return false;
        }
        const uintptr_t site = hit + kReachFirstPersonEffectPatchOffset;
        if (memcmp(reinterpret_cast<const void*>(site), kReachFirstPersonEffectOriginal,
                kReachThirdPersonEffectPatchBytes) != 0)
        {
            LOG("Reach muzzle: third-person deny site does not hold the "
                "expected bytes; leaving it alone");
            return false;
        }
        if (!ReachWriteCode(site, kReachFirstPersonEffectPatch, kReachThirdPersonEffectPatchBytes))
        {
            LOG("Reach muzzle: could not write the third-person suppression");
            return false;
        }
        g_reachThirdPersonEffectPatchSite = site;
        g_reachThirdPersonEffectPatched = true;
        LOG("Reach muzzle: FIRST-person-only particle systems suppressed at "
            "haloreach.dll+0x%llX; the camera-space muzzle flash stops "
            "emitting. Mode 0 and mode 2 systems are untouched",
            static_cast<unsigned long long>(site - base));
        return true;
    }

    bool RestoreReachThirdPersonEffectSuppression()
    {
        if (!g_reachThirdPersonEffectPatched || !g_reachThirdPersonEffectPatchSite)
        {
            return true;
        }
        const bool ok = ReachWriteCode(
            g_reachThirdPersonEffectPatchSite, kReachFirstPersonEffectOriginal, kReachThirdPersonEffectPatchBytes);
        g_reachThirdPersonEffectPatched = false;
        g_reachThirdPersonEffectPatchSite = 0;
        return ok;
    }

    bool ApplyReachNativeWeaponIkBypass()
    {
        uint8_t* const slot = g_reachCamera.nativeWeaponIkDisable;
        uint8_t current = 0;
        if (!slot || !SafeReadByte(slot, &current) || current != g_reachCamera.nativeWeaponIkDisableOriginal ||
            !SafeWriteByte(slot, 1))
        {
            return false;
        }
        g_reachCamera.nativeWeaponIkBypassActive = true;
        if (!SafeReadByte(slot, &current) || current != 1)
            return false;
        LOG("Reach VRIK: native support-hand weapon IK disabled through the "
            "title's exact debug_animation_fp_weapon_ik_disable control; "
            "shared controller solver owns both arms");
        return true;
    }

    bool RestoreReachNativeWeaponIkBypass()
    {
        if (!g_reachCamera.nativeWeaponIkBypassActive)
            return true;
        uint8_t* const slot = g_reachCamera.nativeWeaponIkDisable;
        uint8_t current = 0;
        if (!slot || !SafeReadByte(slot, &current) || current > 1 ||
            !SafeWriteByte(slot, g_reachCamera.nativeWeaponIkDisableOriginal) || !SafeReadByte(slot, &current) ||
            current != g_reachCamera.nativeWeaponIkDisableOriginal)
        {
            return false;
        }
        g_reachCamera.nativeWeaponIkBypassActive = false;
        return true;
    }

    bool RestoreReachLightmapShadowsControl()
    {
        if (!g_reachLightmapShadowRestoreOutstanding.load(std::memory_order_acquire))
        {
            return true;
        }
        uint8_t* const slot = g_reachCamera.lightmapShadowsEnabled;
        const uint8_t original = g_reachLightmapShadowOutstandingValue.load(std::memory_order_relaxed);
        uint8_t current = 0;
        if (!slot || original > 1 || !SafeReadByte(slot, &current) || current > 1 || !SafeWriteByte(slot, original) ||
            !SafeReadByte(slot, &current) || current != original)
        {
            return false;
        }
        g_reachLightmapShadowOutstandingValue.store(0, std::memory_order_relaxed);
        g_reachLightmapShadowRestoreOutstanding.store(false, std::memory_order_release);
        return true;
    }

    struct ReachHrekChudMatchSet
    {
        uintptr_t candidate = 0;
        size_t candidateCount = 0;
        bool malformed = false;
    };

    // Optional, fail-open Reach render hooks (crosshair widget, head-lock observer
    // camera, muzzle effect location, rain): a failed enable logs, removes only
    // this hook and clears its target/trampoline; the camera core continues.
    // Returns true when the hook was created and is now enabled.
    bool EnableOptionalReachHook(
        bool created, void* hook, void*& target, void (*clearOriginal)(), const char* enableFailed)
    {
        if (!created)
            return false;
        if (MH_EnableHook(hook) == MH_OK)
            return true;
        LOG("%s", enableFailed);
        MCCVR_RemoveHookChecked(hook);
        target = nullptr;
        clearOriginal();
        return false;
    }

    bool InstallReachCameraCore(uintptr_t base, size_t size, uint32_t generation)
    {
        if (g_reachCamera.installed.load(std::memory_order_acquire))
            return true;
        if (!base || !generation || size != kReachRetailImageSize)
            return false;
        if (g_reachChudParityFailedGeneration.load(std::memory_order_acquire) == generation)
        {
            return false;
        }
        if (kReachPlayerViewRenderRva >= size || kReachMainRenderViewRva >= size)
            return false;

        const ReachModuleEpoch epoch{base, generation};
        const ReachPreflightToken preflight = ReachRenderCandidate_GetPreflight(epoch);
        if (!preflight.Complete() || !ReachRenderCandidate_IsPreflightCurrent(preflight))
            return false;

        static constexpr char kFpInterpolateAob[] =
            "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57 48 83 EC 20 33 DB 49 63 F8 38 1D ?? ?? ?? ?? 4D 8B E1 8B EA 4C 63 D9";
        static constexpr char kFpPaletteAob[] =
            "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 20 48 8B 05 ?? ?? ?? ?? 49 8B F0 0F B7 C9 4C 8B F2";
        static constexpr char kFpCameraAob[] =
            "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 40 4C 8B 41 08 48 8D 05 ?? ?? ?? ?? 48 8B D9 0F 29 74 24 30";
        if (!ReachColdExactSignatureAt(base, size, kReachFpInterpolateRva, kFpInterpolateAob) ||
            !ReachColdExactSignatureAt(base, size, kReachFpVisiblePaletteRva, kFpPaletteAob) ||
            !ReachColdExactSignatureAt(base, size, kReachFpCameraRebuildRva, kFpCameraAob) ||
            !ReachColdExecutableAddress(base + kReachFpCameraUploadRva))
        {
            LOG("Reach FP install: exact interpolation/palette/camera "
                "signatures failed; stock Reach remains active");
            return false;
        }

        ReachPlayerUnitByOutputUserFn reachPlayerUnitByOutputUser = nullptr;
        ReachUnitInVehicleFn reachUnitInVehicle = nullptr;
        const bool reachVehicleInputResolved =
            ResolveReachVehicleInputBinding(base, size, reachPlayerUnitByOutputUser, reachUnitInVehicle);
        if (!reachVehicleInputResolved)
        {
            LOG("Reach vehicle input: StockFallback; exact HREK/retail "
                "player-unit or all-vehicle proof failed, so the existing "
                "Reach-wide LT/X swap remains active and the camera core "
                "continues");
        }

        ReachPlayerUnitByOutputUserFn reachVehiclePlayerUnit = nullptr;
        ReachUnitGetCameraInfoFn reachUnitGetCameraInfo = nullptr;
        ReachObjectMarkersFn reachObjectMarkers = nullptr;
        ReachObjectUltimateParentFn reachObjectUltimateParent = nullptr;
        ReachVehicleTypeFn reachVehicleType = nullptr;
        ReachTagGetFn reachTagGet = nullptr;
        const bool reachVehicleCameraResolved = ResolveReachVehicleCameraBinding(base, size, reachVehiclePlayerUnit,
            reachUnitGetCameraInfo, reachObjectMarkers, reachObjectUltimateParent, reachVehicleType, reachTagGet);
        if (!reachVehicleCameraResolved)
        {
            LOG("Reach first-person vehicles: StockFallback; exact native "
                "camera/marker/tag identity proof failed. Only vehicle camera "
                "placement stays stock; the Reach stereo core continues");
        }
        else if (!reachObjectUltimateParent)
        {
            LOG("Reach first-person vehicles: attached-weapon ultimate-parent "
                "binding unavailable; those cameras remain active but their "
                "optional carrier view-follow stays stock/world-locked");
        }
        else if (!reachObjectUltimateParent)
        {
            LOG("Reach first-person vehicles: core Installed; attached-gun "
                "carrier follow is StockFallback because its optional ultimate-"
                "parent proof failed");
        }

        uint8_t* reachNativeWeaponIkDisable = nullptr;
        uint8_t reachNativeWeaponIkDisableOriginal = 0;
        if (!ResolveReachNativeWeaponIkControl(
                base, size, reachNativeWeaponIkDisable, reachNativeWeaponIkDisableOriginal))
        {
            LOG("Reach FP install: exact native weapon-IK disable proof failed; "
                "stock Reach remains active");
            return false;
        }

        uint8_t* reachLightmapShadowsEnabled = nullptr;

        // The SSAO isolation, rejected in headset testing, is never armed;
        // its proof code is deleted. The log is unchanged.
        LOG("Reach SSAO candidate intentionally disabled after exact "
            "owned-eye suppression was headset-rejected as the black-world "
            "cause");

        // Resolve the four stock camera-rebuild helpers. The preflight already
        // pinned the exact module SHA-256, so base+RVA is exact; additionally
        // cross-check the two setup call sites so a mismatched image fails open.
        uint8_t* reachPatchyFogFlags = nullptr;
        if (kReachFrustumHelperRva >= size || kReachProjectionBuilderRva >= size ||
            kReachCameraStateUpdaterRva >= size || kReachProjectionMatrixBuilderRva >= size ||
            !ReachVerifyRel32Call(base, kReachSetupFrustumCallRva, kReachFrustumHelperRva) ||
            !ReachVerifyRel32Call(base, kReachSetupProjectionCallRva, kReachProjectionBuilderRva) ||
            !ReachVerifyVisibilityConsumer(base, size) || !ReachVerifyPatchyFogGate(base, size, reachPatchyFogFlags))
        {
            LOG("Reach camera install: camera-rebuild helper verification failed; "
                "stock Reach remains active");
            return false;
        }

        float* const reachMotionBlurScale = FindDebugVarFloat(base, size, "motion_blur_scale");
        float* const reachMotionBlurMax = FindDebugVarFloat(base, size, "motion_blur_max");
        float reachMotionBlurScaleOriginal = 0.0f;
        float reachMotionBlurMaxOriginal = 0.0f;
        if (!reachMotionBlurScale || !reachMotionBlurMax ||
            !ReachMotionBlurSlotsMatchPinnedImage(base, size, reinterpret_cast<uintptr_t>(reachMotionBlurScale),
                reinterpret_cast<uintptr_t>(reachMotionBlurMax)) ||
            !SafeReadFloat(reachMotionBlurScale, &reachMotionBlurScaleOriginal) ||
            !SafeReadFloat(reachMotionBlurMax, &reachMotionBlurMaxOriginal) ||
            !ReachMotionBlurSuppressionValuesValid(reachMotionBlurScaleOriginal, reachMotionBlurMaxOriginal))
        {
            LOG("Reach camera install: exact title-native motion-blur "
                "scale/max proof failed; stock Reach remains active");
            return false;
        }

        // Optional player weather controls. Both are fail-open per feature:
        // an unproven control leaves that ONE effect stock and never blocks the
        // camera core, so a future retail build that moves either one costs the
        // clarity toggle and nothing else.
        auto* const reachRenderRain =
            static_cast<uint8_t*>(FindDebugVarSlot(base, size, "render_rain", kReachDebugVarTypeBoolean));
        uint8_t reachRenderRainOriginal = 0;
        const bool reachRainResolved =
            reachRenderRain &&
            ReachRenderRainSlotMatchesPinnedImage(base, size, reinterpret_cast<uintptr_t>(reachRenderRain)) &&
            SafeReadByte(reachRenderRain, &reachRenderRainOriginal);
        if (!reachRainResolved)
        {
            LOG("Reach weather: the title's render_rain boolean did not resolve "
                "by name against the pinned image; the rain setting stays stock "
                "and the Reach camera core continues");
        }

        const uint32_t* reachAtmosphereFogTlsIndex = nullptr;
        if (!ReachVerifyAtmosphereFogGate(base, size, reachAtmosphereFogTlsIndex))
        {
            LOG("Reach weather: the atmospheric-fog gate proof failed; the fog "
                "setting stays stock and the Reach camera core continues");
        }

        // Once Reach has passed its normal level/liveness proof and
        // camera-core installation is actually beginning, retain the exact title
        // mapping for as long as MinHook owns addresses inside haloreach.dll.
        // Earlier cold/title detection remains identity-only and does not pin the
        // module.
        HMODULE moduleReference = nullptr;
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(base), &moduleReference) ||
            reinterpret_cast<uintptr_t>(moduleReference) != base)
        {
            if (moduleReference)
                FreeLibrary(moduleReference);
            LOG("Reach camera install: exact title mapping is no longer current");
            return false;
        }
        struct ReachInstallModuleReferenceGuard
        {
            HMODULE module = nullptr;
            bool transferred = false;
            ~ReachInstallModuleReferenceGuard()
            {
                if (module && !transferred)
                    FreeLibrary(module);
            }
        } moduleReferenceGuard{moduleReference, false};
        // Optional, not part of core VR ownership: see kReachHudDrawWidgetRva
        // for how this address was found and verified. A failed create/enable
        // here must never affect the five mandatory hooks below.
        void* hudDrawWidget = reinterpret_cast<void*>(base + kReachHudDrawWidgetRva);

        void* inner = reinterpret_cast<void*>(base + kReachPlayerViewRenderRva);
        void* outer = reinterpret_cast<void*>(base + kReachMainRenderViewRva);
        void* fpInterpolate = reinterpret_cast<void*>(base + kReachFpInterpolateRva);
        void* fpPalette = reinterpret_cast<void*>(base + kReachFpVisiblePaletteRva);
        void* fpCamera = reinterpret_cast<void*>(base + kReachFpCameraRebuildRva);
        const bool innerCreated = MCCVR_CreateHookChecked(inner, reinterpret_cast<void*>(&ReachPlayerViewRenderDetour),
                                      reinterpret_cast<void**>(&g_reachOrigPlayerViewRender)) == MH_OK;
        const bool outerCreated =
            innerCreated && MCCVR_CreateHookChecked(outer, reinterpret_cast<void*>(&ReachMainRenderViewDetour),
                                reinterpret_cast<void**>(&g_reachOrigMainRenderView)) == MH_OK;
        const bool fpInterpolateCreated =
            outerCreated && MCCVR_CreateHookChecked(fpInterpolate, reinterpret_cast<void*>(&ReachFpInterpolate),
                                reinterpret_cast<void**>(&g_reachOrigFpInterpolate)) == MH_OK;
        const bool fpPaletteCreated =
            fpInterpolateCreated && MCCVR_CreateHookChecked(fpPalette, reinterpret_cast<void*>(&ReachFpPalette),
                                        reinterpret_cast<void**>(&g_reachOrigFpPalette)) == MH_OK;
        const bool fpCameraCreated =
            fpPaletteCreated && MCCVR_CreateHookChecked(fpCamera, reinterpret_cast<void*>(&ReachFpCameraRebuildDetour),
                                    reinterpret_cast<void**>(&g_reachOrigFpCameraRebuild)) == MH_OK;
        // The SSAO isolation, rejected in headset testing, is never armed.
        void* const ssao = nullptr;
        const bool ssaoCreated = false;
        // Re-enabled with the corrected argument widths. Four prior crashes,
        // all 0xC0000005 at haloreach.dll+0x2ED80C, were caused by this
        // detour declaring arguments 3 and 4 as unsigned short / unsigned
        // char. Reach uses both as full 32-bit values (0x2DA39D
        // "mov r12d, r8d", 0x2DA41D "cmp r12d, dword ptr [rdi+4]", 0x2DA39A
        // "mov eax, r9d"), so the truncated values sent back into the engine
        // selected the wrong branch and decoded an invalid pool handle.
        // The address itself was never wrong: an AOB scan for the class-byte
        // read and forward call-graph tracing from the proven
        // kReachPlayerViewRenderRva independently agree on 0x2DA364.
        const bool hudDrawWidgetCreated =
            fpCameraCreated &&
            MCCVR_CreateHookChecked(hudDrawWidget, reinterpret_cast<void*>(&ReachHudDrawWidgetDetour),
                reinterpret_cast<void**>(&g_reachOrigHudDrawWidget)) == MH_OK;
        if (fpCameraCreated && !hudDrawWidgetCreated)
            LOG("Reach crosshair: chud_draw_widget hook create FAILED at "
                "haloreach.dll+0x%llX",
                static_cast<unsigned long long>(kReachHudDrawWidgetRva));
        // Optional and independent of the crosshair: takes rain, objective
        // markers and character tags off the hand. Deliberately NOT chained off
        // hudDrawWidgetCreated - one optional feature must never gate another.
        void* observerCamera = reinterpret_cast<void*>(base + kReachRenderCameraFromObserverRva);
        const bool observerCameraCreated =
            fpCameraCreated &&
            MCCVR_CreateHookChecked(observerCamera, reinterpret_cast<void*>(&ReachObserverCameraDetour),
                reinterpret_cast<void**>(&g_reachOrigObserverCamera)) == MH_OK;
        if (fpCameraCreated && !observerCameraCreated)
            LOG("Reach head-lock: observer-camera hook create FAILED at "
                "haloreach.dll+0x%llX; rain/markers/tags stay on the hand, "
                "nothing else affected",
                static_cast<unsigned long long>(kReachRenderCameraFromObserverRva));
        // Optional and independent: the rain volume. Located by a UNIQUE
        // interior signature because this function's plain prologue matches
        // three times in the module; the entry is the match minus 0x3C, and it
        // is cross-checked against the expected RVA before use.
        void* rainRender = nullptr;
        bool rainRenderCreated = false;
        // Optional and independent again: the second muzzle flash. The
        // first-person marker query is DECODED from the resolver's own tail
        // jump rather than hardcoded, which also verifies we matched the right
        // function - a wrong match cannot produce a valid E9 there.
        void* effectLocation = reinterpret_cast<void*>(base + kReachEffectLocationResolverRva);
        bool effectLocationCreated = false;
        {
            const uintptr_t jumpSite = base + kReachEffectLocationResolverRva + kReachEffectLocationFpJumpOffset;
            uintptr_t fpQuery = 0;
            if (*reinterpret_cast<const uint8_t*>(jumpSite) == 0xE9)
                fpQuery = sig::RipTarget(jumpSite + 1, jumpSite + 5);
            if (fpQuery < base || fpQuery >= base + size)
            {
                LOG("Reach muzzle: first-person marker query did not decode "
                    "from the resolver tail jump; the second muzzle flash "
                    "stays at the face, nothing else affected");
            }
            else if (fpCameraCreated)
            {
                g_reachEffectFpMarkerQuery = reinterpret_cast<ReachEffectFpMarkerFn>(fpQuery);
                // The marker-not-found fallback matrix this module hands out.
                // Comparing against the engine's own copy is what makes the
                // repair exact rather than a guess about which particle is bad.
                g_reachEffectIdentity = reinterpret_cast<const float*>(base + kReachEffectIdentityMatrixRva);
                g_reachLastGoodValid = false;
                g_reachLastGoodEffect = nullptr;
                effectLocationCreated =
                    MCCVR_CreateHookChecked(effectLocation, reinterpret_cast<void*>(&ReachEffectLocationDetour),
                        reinterpret_cast<void**>(&g_reachOrigEffectLocation)) == MH_OK;
                if (!effectLocationCreated)
                {
                    g_reachEffectFpMarkerQuery = nullptr;
                    LOG("Reach muzzle: effect-location hook create FAILED at "
                        "haloreach.dll+0x%llX",
                        static_cast<unsigned long long>(kReachEffectLocationResolverRva));
                }
                else
                {
                    LOG("Reach muzzle: first-person marker query decoded to "
                        "haloreach.dll+0x%llX (expected 0x%llX)",
                        static_cast<unsigned long long>(fpQuery - base),
                        static_cast<unsigned long long>(kReachEffectFpMarkerQueryRva));
                }
            }
        }
        if (!innerCreated || !outerCreated || !fpInterpolateCreated || !fpPaletteCreated || !fpCameraCreated)
        {
            // Roll back every hook created so far, newest first. A removal
            // MinHook cannot confirm keeps that hook (target and trampoline)
            // owned by a retained, teardown-requested core for verified cleanup.
            struct CreatedReachHook
            {
                bool created;
                void* hook;
                void** coreTarget;
                void (*clearOriginal)();
                bool retained;
            };
            CreatedReachHook created[] = {
                {rainRenderCreated, rainRender, &g_reachCamera.rainRenderTarget,
                    [] { g_reachOrigRainRender = nullptr; }, false},
                {effectLocationCreated, effectLocation, &g_reachCamera.effectLocationTarget,
                    []
                    {
                        g_reachOrigEffectLocation = nullptr;
                        g_reachEffectFpMarkerQuery = nullptr;
                    },
                    false},
                {observerCameraCreated, observerCamera, &g_reachCamera.observerCameraTarget,
                    [] { g_reachOrigObserverCamera = nullptr; }, false},
                {hudDrawWidgetCreated, hudDrawWidget, &g_reachCamera.hudDrawWidgetTarget,
                    [] { g_reachOrigHudDrawWidget = nullptr; }, false},
                {ssaoCreated, ssao, &g_reachCamera.ssaoTarget, [] { g_reachOrigRenderSsao = nullptr; }, false},
                {fpCameraCreated, fpCamera, &g_reachCamera.fpCameraTarget, [] { g_reachOrigFpCameraRebuild = nullptr; },
                    false},
                {fpPaletteCreated, fpPalette, &g_reachCamera.fpPaletteTarget, [] { g_reachOrigFpPalette = nullptr; },
                    false},
                {fpInterpolateCreated, fpInterpolate, &g_reachCamera.fpInterpolateTarget,
                    [] { g_reachOrigFpInterpolate = nullptr; }, false},
                {outerCreated, outer, &g_reachCamera.outerTarget, [] { g_reachOrigMainRenderView = nullptr; }, false},
                {innerCreated, inner, &g_reachCamera.innerTarget, [] { g_reachOrigPlayerViewRender = nullptr; }, false},
            };
            bool cleanupOk = true;
            for (CreatedReachHook& hook : created)
            {
                hook.retained = hook.created;
                if (hook.created)
                {
                    const MH_STATUS status = MCCVR_RemoveHookChecked(hook.hook);
                    if (status == MH_OK || status == MH_ERROR_NOT_CREATED)
                        hook.retained = false;
                    else
                        cleanupOk = false;
                }
                if (!hook.retained)
                    hook.clearOriginal();
            }
            if (!cleanupOk)
            {
                g_reachCamera.base = base;
                g_reachCamera.size = size;
                g_reachCamera.generation = generation;
                g_reachCamera.moduleReference = moduleReference;
                moduleReferenceGuard.transferred = true;
                for (const CreatedReachHook& hook : created)
                    *hook.coreTarget = hook.retained ? hook.hook : nullptr;
                g_reachCamera.ssaoIsolationActive.store(false, std::memory_order_release);
                g_reachCamera.armed.store(false, std::memory_order_release);
                g_reachCamera.teardownRequested.store(true, std::memory_order_release);
                g_reachCamera.installed.store(true, std::memory_order_release);
            }
            LOG("Reach camera install: mandatory parity hook creation failed; "
                "the complete Reach VR transaction was rejected");
            return false;
        }

        g_reachHelpers.frustum = reinterpret_cast<ReachFrustumHelperFn>(base + kReachFrustumHelperRva);
        g_reachHelpers.projection = reinterpret_cast<ReachProjectionBuilderFn>(base + kReachProjectionBuilderRva);
        g_reachHelpers.cameraState = reinterpret_cast<ReachCameraStateUpdaterFn>(base + kReachCameraStateUpdaterRva);
        g_reachHelpers.matrix = reinterpret_cast<ReachMatrixBuilderFn>(base + kReachProjectionMatrixBuilderRva);
        g_reachHelpers.commitOuterCamera =
            reinterpret_cast<ReachCameraStackCallbackFn>(base + kReachCameraStackCallbackRva);
        g_reachCamera.base = base;
        g_reachCamera.size = size;
        g_reachCamera.decoratorWindState = ResolveReachDecoratorWind(base, size);
        g_reachWindReplayFaults.store(0, std::memory_order_relaxed);
        g_reachWindReplayPairs.store(0, std::memory_order_relaxed);
        LOG("Reach decorator wind: %s; native wind advances once per stereo pair; camera/LOD remain native",
            g_reachCamera.decoratorWindState ? "Installed" : "StockFallback (binding unavailable)");
        g_reachCamera.generation = generation;
        PrepareCachedPhysicalCrouch(GameTitle::HaloReach, base, size, generation);
        g_reachCamera.moduleReference = moduleReference;
        moduleReferenceGuard.transferred = true;
        g_reachCamera.innerTarget = inner;
        g_reachCamera.outerTarget = outer;
        g_reachCamera.fpInterpolateTarget = fpInterpolate;
        g_reachCamera.fpPaletteTarget = fpPalette;
        g_reachCamera.fpCameraTarget = fpCamera;
        g_reachCamera.ssaoTarget = ssaoCreated ? ssao : nullptr;
        g_reachCamera.ssaoIsolationActive.store(false, std::memory_order_release);
        g_reachCamera.hudDrawWidgetTarget = hudDrawWidgetCreated ? hudDrawWidget : nullptr;
        g_reachCamera.observerCameraTarget = observerCameraCreated ? observerCamera : nullptr;
        g_reachCamera.effectLocationTarget = effectLocationCreated ? effectLocation : nullptr;
        g_reachCamera.rainRenderTarget = rainRenderCreated ? rainRender : nullptr;
        g_reachCamera.playerUnitByOutputUser = reachVehicleInputResolved ? reachPlayerUnitByOutputUser : nullptr;
        g_reachCamera.unitInVehicle = reachVehicleInputResolved ? reachUnitInVehicle : nullptr;
        g_reachVehicleInputSnapshot.store(0, std::memory_order_release);
        g_reachVehicleInputFaults.store(0, std::memory_order_relaxed);
        g_reachCamera.vehicleInputBindingState.store(
            static_cast<uint8_t>(reachVehicleInputResolved ? ReachVehicleInputBindingState::Installed
                                                           : ReachVehicleInputBindingState::StockFallback),
            std::memory_order_release);
        if (reachVehicleInputResolved)
        {
            LOG("Reach vehicle input: Installed; native LT/X will be restored "
                "for HREK unit_in_vehicle seats; the richer camera-info sampler "
                "supersedes it for mounted turret types when available");
        }
        g_reachCamera.vehiclePlayerUnitByOutputUser = reachVehicleCameraResolved ? reachVehiclePlayerUnit : nullptr;
        g_reachCamera.unitGetCameraInfo = reachVehicleCameraResolved ? reachUnitGetCameraInfo : nullptr;
        g_reachCamera.objectMarkers = reachVehicleCameraResolved ? reachObjectMarkers : nullptr;
        g_reachCamera.objectUltimateParent = reachVehicleCameraResolved ? reachObjectUltimateParent : nullptr;
        g_reachCamera.vehicleType = reachVehicleCameraResolved ? reachVehicleType : nullptr;
        g_reachCamera.tagGet = reachVehicleCameraResolved ? reachTagGet : nullptr;
        g_reachCamera.vehicleCameraBindingState.store(
            static_cast<uint8_t>(reachVehicleCameraResolved ? ReachVehicleCameraBindingState::Installed
                                                            : ReachVehicleCameraBindingState::StockFallback),
            std::memory_order_release);
        g_reachVehicleTrimSnapshot.store(0, std::memory_order_release);
        g_reachVehicleSampleMs.store(0, std::memory_order_relaxed);
        g_reachVehicleFpStable.store(static_cast<uint32_t>(Halo3VehicleState::Unknown), std::memory_order_relaxed);
        g_reachVehicleCameraFaults.store(0, std::memory_order_relaxed);
        g_reachVehicleIdentityMisses.store(0, std::memory_order_relaxed);
        g_reachVehicleMarkerFailures.store(0, std::memory_order_relaxed);
        g_reachVehicleBoundsFailures.store(0, std::memory_order_relaxed);
        g_reachVehicleBounceFallbacks.store(0, std::memory_order_relaxed);
        g_reachVehicleFollowFallbacks.store(0, std::memory_order_relaxed);
        g_reachVehicleSeatPatchState.store(
            static_cast<uint32_t>(ReachSeatLeaseState::Empty), std::memory_order_relaxed);
        g_reachVehicleSeatPatchSerial.store(0, std::memory_order_relaxed);
        g_reachVehicleSeatPatchFailures.store(0, std::memory_order_relaxed);
        g_reachUnitCameraHideSets.store(0, std::memory_order_relaxed);
        g_reachUnitCameraHideAlreadySet.store(0, std::memory_order_relaxed);
        g_reachUnitCameraHideRestores.store(0, std::memory_order_relaxed);
        g_reachUnitCameraHideFailures.store(0, std::memory_order_relaxed);
        g_reachVehicleSeatRecenters.store(0, std::memory_order_relaxed);
        g_reachVehicleExitRecenters.store(0, std::memory_order_relaxed);
        g_reachFpLegPaletteObservationCount.store(0, std::memory_order_relaxed);
        g_reachFpLegPalettePreserved.store(0, std::memory_order_relaxed);
        g_reachFpLegPaletteIdentityRejects.store(0, std::memory_order_relaxed);
        g_reachFpLegPaletteBuildFailures.store(0, std::memory_order_relaxed);
        g_reachSeatAuthorsSteering.store(0, std::memory_order_relaxed);
        ReachClearAimFeedback();
        ReachClearCompletedFrameFeedback();
        ReachPublishShotOccupation(0, nullptr);
        g_reachUnitAimSamples.store(0, std::memory_order_relaxed);
        g_reachUnitAimFallbacks.store(0, std::memory_order_relaxed);
        g_reachAimFeedbackReadFallbacks.store(0, std::memory_order_relaxed);
        g_reachReticleAimFallbacks.store(0, std::memory_order_relaxed);
        ReachClearHullHeading();
        g_reachVehicleFpDebounce = {};
        g_reachSeatRecenterLatch.Clear();
        g_reachYawReferencePair.store(kReachInvalidYawReferencePair, std::memory_order_release);
        g_reachHeadReference = {};
        g_reachSeatAnchorLatch = {};
        g_reachVehicleAnchorFallbacks.store(0, std::memory_order_relaxed);
        g_reachNativeSeatLease = {};
        g_reachSeatYaw = {};
        g_reachYawSeat = {};
        if (reachVehicleCameraResolved)
        {
            LOG("Reach first-person vehicles: Installed; every structurally "
                "proven seat gets the camera (%d exact HREK identities carry "
                "per-seat trims and policy, unmatched vehicles ride the "
                "universal trims); native unit-aim feedback is armed for view "
                "follow OFF and ON, with rendered marker placement, a "
                "render-scoped native unit-camera body hide, bounce and "
                "hands",
                kReachVehicleIdentityCount);
        }

        g_reachFirstPersonRenderGateReturn.store(0, std::memory_order_release);
        g_reachPassengerFirstPersonAdmissions.store(0, std::memory_order_relaxed);
        {
            uintptr_t renderGate = 0;
            uintptr_t renderReturn = 0;
            const bool renderGateResolved =
                ResolveReachFirstPersonRenderGateBinding(base, size, renderGate, renderReturn);
            if (renderGateResolved)
            {
                const GatedHook hook = InstallGatedHook(g_reachFirstPersonRenderGateTarget,
                    reinterpret_cast<void*>(renderGate), reinterpret_cast<void*>(&ReachFirstPersonRenderGateHook),
                    reinterpret_cast<void**>(&g_origReachFirstPersonRenderGate));
                if (hook.enabled)
                {
                    g_reachFirstPersonRenderGateReturn.store(renderReturn, std::memory_order_release);
                    LOG("Reach scope first-person render gate: Installed "
                        "at the exact render_first_person_view admission "
                        "predicate (+0x%llX, caller +0x%llX); scope suppresses "
                        "native first-person pixels; passenger admission stays disabled",
                        (unsigned long long)(renderGate - base), (unsigned long long)(renderReturn - base));
                }
                else
                {
                    LOG("Reach scope first-person suppression: FAILED to "
                        "install the exact render admission hook (create=%d, "
                        "cleanup=%s); separate scope stays unavailable while "
                        "the stereo camera stays active",
                        (int)hook.created, hook.retained ? "retained for verified teardown" : "complete");
                }
            }
            else if (reachVehicleCameraResolved)
            {
                LOG("Reach passenger first-person presentation: the passenger "
                    "render admission detour is DISABLED - it ran a whole "
                    "headset session, including a Warthog passenger seat, "
                    "with zero admissions, so it is not the mechanism that "
                    "suppresses the seated first-person hands/gun. Passenger "
                    "hands remain stock; the seat log now names the live seat "
                    "flags and the shot line names why a personal shot kept "
                    "its stock origin");
            }
        }

        // The shot line. Resolve the camera-position evaluator and
        // its two firing call sites; their return addresses are what tell the
        // hook it is on the firing path rather than one of the evaluator's 32
        // other consumers - including the engine's own camera placement,
        // which must keep the stock value.
        RevokeReachFiringOriginFeature();
        g_reachFiringOriginMoves.store(0, std::memory_order_relaxed);
        g_reachFiringOriginSkips.store(0, std::memory_order_relaxed);
        {
            uintptr_t firingEvaluator = 0;
            uintptr_t firingReturnA = 0;
            uintptr_t firingReturnB = 0;
            const bool firingResolved =
                reachVehicleCameraResolved &&
                ResolveReachFiringOriginBinding(base, size, firingEvaluator, firingReturnA, firingReturnB);
            if (firingResolved)
            {
                const GatedHook hook = InstallGatedHook(g_reachUnitCameraPositionTarget,
                    reinterpret_cast<void*>(firingEvaluator), reinterpret_cast<void*>(&ReachUnitCameraPositionHook),
                    reinterpret_cast<void**>(&g_origReachUnitCameraPosition));
                if (hook.enabled)
                {
                    g_reachFiringOriginReturnA.store(firingReturnA, std::memory_order_release);
                    g_reachFiringOriginReturnB.store(firingReturnB, std::memory_order_release);
                    LOG("Reach seat aim: shot origin follows the rendered eye "
                        "(evaluator +0x%llX, firing returns +0x%llX/+0x%llX); "
                        "only an allows-weapons seat's own personal fire "
                        "moves, so the engine camera, vehicle barrels and "
                        "cutscenes keep the stock position",
                        (unsigned long long)(firingEvaluator - base), (unsigned long long)(firingReturnA - base),
                        (unsigned long long)(firingReturnB - base));
                }
                else
                {
                    LOG("Reach seat aim: FAILED to install the shot-origin "
                        "hook (create=%d, cleanup=%s); seated personal-weapon "
                        "shots keep "
                        "firing from the engine's own eye point and will not "
                        "follow the crosshair exactly",
                        (int)hook.created, hook.retained ? "retained for verified teardown" : "complete");
                }
            }
            else if (reachVehicleCameraResolved)
            {
                LOG("Reach seat aim: shot-origin signatures missing or "
                    "ambiguous; seated personal-weapon shots keep firing from "
                    "the engine's own eye point and will not follow the "
                    "crosshair exactly");
            }
        }
        g_reachVehicleShotRedirectEnabled.store(false, std::memory_order_release);
        g_reachVehicleShotRedirects.store(0, std::memory_order_relaxed);
        g_reachVehicleShotRedirectMisses.store(0, std::memory_order_relaxed);
        {
            uintptr_t unitAdjust = 0;
            const bool unitAdjustResolved =
                reachVehicleCameraResolved && ResolveReachUnitAdjustBinding(base, size, unitAdjust);
            if (unitAdjustResolved)
            {
                const GatedHook hook = InstallGatedHook(g_reachUnitAdjustTarget, reinterpret_cast<void*>(unitAdjust),
                    reinterpret_cast<void*>(&ReachUnitAdjustHook), reinterpret_cast<void**>(&g_origReachUnitAdjust));
                if (hook.enabled)
                {
                    g_reachVehicleShotRedirectEnabled.store(true, std::memory_order_release);
                    LOG("Reach vehicle shot line: Installed at native "
                        "unit-adjust +0x%llX; the exact local selected barrel "
                        "keeps its stock origin and downstream spread/tracking, "
                        "while its central direction passes through the "
                        "presented VR reticle",
                        (unsigned long long)(unitAdjust - base));
                }
                else
                {
                    LOG("Reach vehicle shot line: FAILED to install "
                        "(create=%d, cleanup=%s); vehicle shot alignment is "
                        "unavailable, while the camera core remains active",
                        (int)hook.created, hook.retained ? "retained for verified teardown" : "complete");
                }
            }
            else if (reachVehicleCameraResolved)
            {
                LOG("Reach vehicle shot line: unit-adjust binding missing or "
                    "ambiguous; vehicle shot alignment is unavailable, while "
                    "the camera core remains active");
            }
        }

        // Reach game brightness. The exact homologue of the Halo 3/ODST screen
        // colour/gamma publisher, pinned by RVA inside the already whole-image-
        // hashed module and re-proved here by its own Reach-only entry AOB.
        // Missing proof leaves only this control stock.
        {
            static constexpr char kReachScreenColorAob[] = "48 83 EC 48 0F 29 74 24 30 0F 28 F1 0F 29 7C 24 20 "
                                                           "0F 28 CA 0F 28 F8";
            const bool proven = ReachColdExactSignatureAt(base, size, kReachScreenColorUploadRva, kReachScreenColorAob);
            if (proven)
            {
                const GatedHook hook =
                    InstallGatedHook(g_reachHudXformTarget, reinterpret_cast<void*>(base + kReachScreenColorUploadRva),
                        reinterpret_cast<void*>(&ReachHudXformDetour), reinterpret_cast<void**>(&g_origReachHudXform));
                if (hook.enabled)
                {
                    LOG("Reach brightness: Installed at haloreach.dll+0x%llX; "
                        "game_brightness now scales Reach's screen colour/gamma "
                        "exactly as it does Halo 3's and ODST's",
                        (unsigned long long)kReachScreenColorUploadRva);
                }
                else
                {
                    LOG("Reach brightness: FAILED to install (create=%d, "
                        "cleanup=%s); Reach keeps the game's own brightness "
                        "while the camera core remains active",
                        (int)hook.created, hook.retained ? "retained for verified teardown" : "complete");
                }
            }
            else
            {
                LOG("Reach brightness: exact screen colour/gamma proof failed "
                    "at +0x%llX; Reach keeps the game's own brightness while "
                    "the camera core remains active",
                    (unsigned long long)kReachScreenColorUploadRva);
            }
        }

        // Reach HUD height matches Halo 3's native anchor translation, using
        // Reach's independently proven six-argument producer. This optional
        // feature never participates in camera ownership or arming.
        // Assigned here, not by a dynamic initialiser at the include, so no other file's static
        // initialisation can see it unset or set: the pointer is constinit nullptr and only this
        // hook's detour reads it, after the create below.
        g_reachMotionShiftHook = &ReachShiftSensorOrigin;
        g_reachHudHeightEnabled.store(false, std::memory_order_release);
        g_reachHudHeightApplied.store(0, std::memory_order_relaxed);
        g_reachHudHeightRefused.store(0, std::memory_order_relaxed);
        {
            DWORD64 functionBase = 0;
            const PRUNTIME_FUNCTION function =
                RtlLookupFunctionEntry(base + kReachHudAnchorBasisRva, &functionBase, nullptr);
            const bool proven =
                kReachHudAnchorBasisEndRva <= size && function && functionBase == base &&
                function->BeginAddress == kReachHudAnchorBasisRva &&
                function->EndAddress == kReachHudAnchorBasisEndRva &&
                ReachColdExactSignatureAt(base, size, kReachHudAnchorBasisRva, kReachHudAnchorBasisEntryAob) &&
                ReachColdExactSignatureAt(base, size, kReachHudAnchorBasisOutputRva, kReachHudAnchorBasisOutputAob) &&
                ReachVerifyRel32Call(base, kReachHudAnchorBitmapCallRva, kReachHudAnchorBasisRva) &&
                ReachVerifyRel32Call(base, kReachHudAnchorTextCallRva, kReachHudAnchorBasisRva) &&
                ReachVerifyRel32Call(base, kReachHudAnchorModelCallRva, kReachHudAnchorBasisRva) &&
                ReachVerifyRel32Call(base, kReachHudAnchorParentCallRva, kReachHudAnchorBasisRva);
            if (proven)
            {
                g_reachHudAnchorBasisTarget = reinterpret_cast<void*>(base + kReachHudAnchorBasisRva);
                const MH_STATUS created = MCCVR_CreateHookChecked(g_reachHudAnchorBasisTarget,
                    reinterpret_cast<void*>(&ReachHudAnchorBasisDetour),
                    reinterpret_cast<void**>(&g_reachOrigHudAnchorBasis));
                const bool enabled = created == MH_OK && MH_EnableHook(g_reachHudAnchorBasisTarget) == MH_OK;
                g_reachHudHeightEnabled.store(enabled, std::memory_order_release);
                if (enabled)
                {
                    LOG("Reach HUD height: Installed at haloreach.dll+0x%llX; "
                        "native six-argument anchor basis, positive raises, "
                        "negative lowers; redirected reticle stays on aim",
                        static_cast<unsigned long long>(kReachHudAnchorBasisRva));
                }
                else
                {
                    // Never remove a possibly active trampoline here. A
                    // failed enable remains stock and retained for the normal
                    // frozen-thread/callback-verified title teardown.
                    if (created != MH_OK)
                    {
                        g_reachHudAnchorBasisTarget = nullptr;
                        g_reachOrigHudAnchorBasis = nullptr;
                    }
                    LOG("Reach HUD height: StockFallback (create=%d); "
                        "only HUD height stays stock, camera core continues%s",
                        static_cast<int>(created), created == MH_OK ? "; hook retained for verified teardown" : "");
                }
            }
            else
            {
                LOG("Reach HUD height: StockFallback; unique native entry, "
                    "basis-output or bitmap/text/model caller proof failed; "
                    "camera core continues");
            }
        }

        g_reachRainDecoupled.store(0, std::memory_order_relaxed);
        g_reachRainSkipped.store(0, std::memory_order_relaxed);
        g_reachMuzzleRedirects.store(0, std::memory_order_relaxed);
        g_reachMuzzleRepaired.store(0, std::memory_order_relaxed);
        g_reachMuzzleReparented.store(0, std::memory_order_relaxed);
        g_reachLiveGraphWeaponWrites.store(0, std::memory_order_relaxed);
        g_reachWorldWristWritesPrevented.store(0, std::memory_order_relaxed);
        g_reachHudFlashHidden.store(0, std::memory_order_relaxed);
        for (auto& slot : g_reachChudMaxCollection)
            slot.store(0, std::memory_order_relaxed);
        g_reachMuzzleOutOfRange.store(0, std::memory_order_relaxed);
        g_reachMuzzleNearestMilli.store(0xFFFFFFFFu, std::memory_order_relaxed);
        g_reachMuzzleIdentityNoSibling.store(0, std::memory_order_relaxed);
        g_reachMuzzleReadFailures.store(0, std::memory_order_relaxed);
        g_reachMuzzleWorldNoFpUser.store(0, std::memory_order_relaxed);
        g_reachMuzzleWorldFpNone.store(0, std::memory_order_relaxed);
        g_reachMuzzleAlreadyFp.store(0, std::memory_order_relaxed);
        g_reachMuzzleFpByteLowMask.store(0, std::memory_order_relaxed);
        g_reachMuzzleFpByteHighMask.store(0, std::memory_order_relaxed);
        for (int site = 0; site < 6; ++site)
        {
            g_reachObserverCameraSiteHits[site].store(0, std::memory_order_relaxed);
            g_reachObserverCameraCorrected[site].store(0, std::memory_order_relaxed);
        }
        g_reachObserverCameraUnknownSite.store(0, std::memory_order_relaxed);
        g_reachFpCameraUpload = reinterpret_cast<ReachFpCameraUploadFn>(base + kReachFpCameraUploadRva);
        g_reachCamera.installedAtMs = GetTickCount64();
        g_reachCamera.motionBlurVars[0] = {reachMotionBlurScale, reachMotionBlurScaleOriginal};
        g_reachCamera.motionBlurVars[1] = {reachMotionBlurMax, reachMotionBlurMaxOriginal};
        g_reachCamera.motionBlurResolved = true;
        g_reachCamera.motionBlurSuppressed = false;
        g_reachCamera.patchyFogFlags = reachPatchyFogFlags;
        g_reachCamera.renderRainSlot = reachRainResolved ? reachRenderRain : nullptr;
        g_reachCamera.renderRainOriginal = reachRainResolved ? reachRenderRainOriginal : 1;
        g_reachCamera.renderRainSuppressed = false;
        g_reachCamera.atmosphereFogTlsIndex = reachAtmosphereFogTlsIndex;
        g_reachCamera.atmosphereFogSuppressed = false;
        g_reachRainSuppressedEyes.store(0, std::memory_order_relaxed);
        g_reachFogSuppressedEyes.store(0, std::memory_order_relaxed);
        g_reachWeatherWriteFailures.store(0, std::memory_order_relaxed);
        g_reachFogFlagsUnresolved.store(0, std::memory_order_relaxed);
        LOG("Reach weather controls: rain %s, atmospheric fog %s "
            "(settings: rain %s, fog %s)",
            reachRainResolved ? "bound by name" : "STOCK (unproven)",
            reachAtmosphereFogTlsIndex ? "bound at its gate" : "STOCK (unproven)", g_config.rain ? "on" : "off",
            g_config.atmospheric_fog ? "on" : "off");
        g_reachCamera.lightmapShadowsEnabled = reachLightmapShadowsEnabled;
        g_reachLightmapShadowSuppressedEyes.store(0, std::memory_order_relaxed);
        g_reachLightmapShadowAlreadyDisabledEyes.store(0, std::memory_order_relaxed);
        g_reachLightmapShadowWriteFailures.store(0, std::memory_order_relaxed);
        g_reachLightmapShadowRestoreFailures.store(0, std::memory_order_relaxed);
        g_reachLightmapShadowOutstandingValue.store(0, std::memory_order_relaxed);
        g_reachLightmapShadowRestoreOutstanding.store(false, std::memory_order_release);
        for (auto& count : g_reachSsaoSuppressedEyeCalls)
            count.store(0, std::memory_order_relaxed);
        g_reachSsaoPassthroughCalls.store(0, std::memory_order_relaxed);
        g_reachCamera.nativeWeaponIkDisable = reachNativeWeaponIkDisable;
        g_reachCamera.nativeWeaponIkDisableOriginal = reachNativeWeaponIkDisableOriginal;
        g_reachCamera.nativeWeaponIkBypassActive = false;
        for (int eye = 0; eye < 2; ++eye)
        {
            g_reachRenderFovSerial[eye].store(0, std::memory_order_release);
            g_reachRenderHalfFovX[eye].store(0.0f, std::memory_order_relaxed);
            g_reachRenderHalfFovY[eye].store(0.0f, std::memory_order_relaxed);
        }
        g_reachFpCameraUploadStatus.preparedSerial.store(0, std::memory_order_release);
        g_reachFpCameraUploadStatus.generation.store(0, std::memory_order_relaxed);
        g_reachFpCameraUploadStatus.eyeMask.store(0, std::memory_order_relaxed);
        g_reachFpCameraLoggedGeneration.store(0, std::memory_order_release);
        g_reachOuterCameraCommitStatus.preparedSerial.store(0, std::memory_order_release);
        g_reachOuterCameraCommitStatus.generation.store(0, std::memory_order_relaxed);
        g_reachOuterCameraCommitStatus.eyeMask.store(0, std::memory_order_relaxed);
        g_reachOuterCameraCommitLoggedGeneration.store(0, std::memory_order_release);
        g_aimSeen.store(false, std::memory_order_release);
        g_camValid.store(false, std::memory_order_release);
        g_baseCamValid.store(false, std::memory_order_release);
        g_reachCamera.armed.store(false, std::memory_order_release);
        g_reachCamera.teardownRequested.store(false, std::memory_order_release);
        g_reachCamera.installed.store(true, std::memory_order_release);
        g_needRecenter.store(true, std::memory_order_release);
        if (MH_EnableHook(inner) != MH_OK || MH_EnableHook(outer) != MH_OK || MH_EnableHook(fpInterpolate) != MH_OK ||
            MH_EnableHook(fpPalette) != MH_OK || MH_EnableHook(fpCamera) != MH_OK)
        {
            // State was published before the first enable, so even a partial
            // MinHook failure can use the same disable/quiesce/remove proof as
            // a title transition. A failed proof retains every dependency and
            // teardownRequested makes the worker retry instead of re-arming.
            LOG("Reach camera install: MinHook enable failed; requesting "
                "verified teardown");
            RemoveReachCameraCore();
            return false;
        }
        // DISABLED after a headset test. The patch verifiably
        // applied (log: "suppressed at haloreach.dll+0x1D4F18") and denied
        // camera-mode-2 particle systems engine-wide, and the face-stuck flash
        // kept rendering. So the element is NOT a third-person-only particle
        // system. Combined with the earlier eliminations this rules out the
        // entire per-mode emission gate as the source. Kept for evidence.
        // DISABLED after the decisive headset test: denying mode-1
        // removed BOTH muzzle flashes - the face one AND the gun one. That is
        // the proof that both are first-person-only particle systems; the
        // difference between them is only WHERE their spawn transform comes
        // from. A blanket denial is therefore too broad, per the player:
        // "restore the muzzle flashes". The selective fix must retarget the
        // stuck system's spawn transform instead of gating emission.
        // ApplyReachThirdPersonEffectSuppression(base, size);
        ApplyReachSkyParallaxNeutralization(base, size);
        if (!ApplyReachNativeWeaponIkBypass())
        {
            LOG("Reach camera install: native weapon-IK bypass failed; "
                "requesting verified teardown of the complete parity transaction");
            RemoveReachCameraCore();
            return false;
        }
        // HREK reach_tag_test.exe+0x4166A0 proves the same swept-point
        // collision behavior as Halo 3, but the optimized retail wrapper
        // specializes the second ignored object to NONE and exposes four
        // arguments. Keep that engine-specific ABI in its own detour; a
        // missing/moved signature leaves only this optional feature stock.
        static constexpr char kReachCollisionResolveSignature[] =
            "48 89 5C 24 20 55 56 57 41 56 41 57 48 8D 6C 24 D1 48 81 EC "
            "D0 00 00 00 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 45 1F 41 83 "
            "CF FF 48 8D 45 AF 48 89 44 24 38 49 8B F8";
        static constexpr char kReachCollisionVectorSignature[] =
            "48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 "
            "48 8D AC 24 F0 97 FE FF B8 10 69 01 00 E8 ?? ?? ?? ?? "
            "48 2B E0 0F 29 B4 24 00 69 01 00 0F 29 BC 24 F0 68 01 00";
        InstallLegacyWorldCollision(g_reachWorldCollision, GameTitle::HaloReach, base, size, generation,
            reinterpret_cast<void*>(&ReachCollisionVectorDetour), 0x0012C5D4, kReachCollisionResolveSignature, true,
            0x0012969C, kReachCollisionVectorSignature);
        InstallReachContactMelee(base, size, generation);
        (void)InstallReachMuzzle(base, size, generation);
        (void)InstallReachWeaponHaptics(base, size, generation);
        (void)InstallReachGrabPickup(base, size, generation);
        LOG("Reach camera core installed: outer/inner stereo + FP "
            "interpolation/palette + per-eye world-projection camera "
            "transactions hooked; waiting one-second fresh-camera interval "
            "before arming");
        // Optional, feature-local candidate. Its exact return-address and
        // thread-local/live ownership gates keep flat, nested, screenshot, and
        // unrelated native calls on the original SSAO path. A create/enable
        // failure never tears down the working Reach camera core.
        if (ssaoCreated)
        {
            const MH_STATUS enableStatus = MH_EnableHook(ssao);
            if (enableStatus == MH_OK)
            {
                g_reachCamera.ssaoIsolationActive.store(true, std::memory_order_release);
                LOG("Reach SSAO candidate BOUND with zero samples: exact native "
                    "SSAO/HDAO +0x%llX will be skipped only for the exact owned "
                    "VR-eye call +0x%llX; all other calls remain stock",
                    static_cast<unsigned long long>(kReachSsaoRva), static_cast<unsigned long long>(kReachSsaoCallRva));
            }
            else
            {
                g_reachCamera.ssaoIsolationActive.store(false, std::memory_order_release);
                const MH_STATUS disableStatus = MH_DisableHook(ssao);
                const MH_STATUS removeStatus = MCCVR_RemoveHookChecked(ssao);
                if (removeStatus == MH_OK || removeStatus == MH_ERROR_NOT_CREATED)
                {
                    g_reachCamera.ssaoTarget = nullptr;
                    g_reachOrigRenderSsao = nullptr;
                }
                else
                {
                    LOG("Reach SSAO candidate enable failed (%d), and its "
                        "hook disable/remove failed (%d/%d); retained for "
                        "verified title teardown",
                        static_cast<int>(enableStatus), static_cast<int>(disableStatus),
                        static_cast<int>(removeStatus));
                }
                LOG("Reach SSAO candidate not armed; native SSAO remains active "
                    "and the Reach camera core continues");
            }
        }
        // Optional and fail-open, same as the mandatory five are not: a failed
        // enable here removes only this one hook and continues, exactly like a
        // failed create above. Never RemoveReachCameraCore() for this hook.
        if (EnableOptionalReachHook(
                hudDrawWidgetCreated, hudDrawWidget, g_reachCamera.hudDrawWidgetTarget,
                [] { g_reachOrigHudDrawWidget = nullptr; },
                "Reach crosshair: chud_draw_widget hook enable failed; "
                "native crosshair stays visible, nothing else affected"))
        {
            LOG("Reach crosshair: chud_draw_widget hook active at "
                "haloreach.dll+0x%llX; authored-widget capture pending "
                "per-eye execution",
                static_cast<unsigned long long>(kReachHudDrawWidgetRva));
        }
        // Same optional, fail-open contract: a failure here removes only this
        // hook. The world render camera is never corrected by it either way, so
        // the accepted Reach 3D cannot be affected by this feature failing.
        if (EnableOptionalReachHook(
                observerCameraCreated, observerCamera, g_reachCamera.observerCameraTarget,
                [] { g_reachOrigObserverCamera = nullptr; },
                "Reach head-lock: observer-camera hook enable failed; rain, "
                "objective markers and character tags stay on the hand, "
                "nothing else affected"))
        {
            LOG("Reach head-lock: observer-camera hook active at "
                "haloreach.dll+0x%llX; rain/objective markers/character tags "
                "will follow the head only",
                static_cast<unsigned long long>(kReachRenderCameraFromObserverRva));
        }
        if (EnableOptionalReachHook(
                effectLocationCreated, effectLocation, g_reachCamera.effectLocationTarget,
                []
                {
                    g_reachOrigEffectLocation = nullptr;
                    g_reachEffectFpMarkerQuery = nullptr;
                },
                "Reach muzzle: effect-location hook enable failed; the second "
                "muzzle flash stays at the face, nothing else affected"))
        {
            LOG("Reach muzzle: effect-location hook active at "
                "haloreach.dll+0x%llX; world locations of first-person weapon "
                "effects will resolve on the gun",
                static_cast<unsigned long long>(kReachEffectLocationResolverRva));
        }
        if (EnableOptionalReachHook(
                rainRenderCreated, rainRender, g_reachCamera.rainRenderTarget, [] { g_reachOrigRainRender = nullptr; },
                "Reach rain: renderer hook enable failed; the rain keeps "
                "swinging with the view, nothing else affected"))
        {
            LOG("Reach rain: renderer hook active; the rain volume will centre "
                "on the camera instead of swinging along its forward vector");
        }
        // Optional and fail-open: kill_reticle only. If this cannot be resolved
        // exactly, Reach keeps its stock crosshair and nothing else changes.
        if (!ResolveReachChudCrosshairFields(base, size))
            LOG("Reach crosshair: chud_show_crosshair script-table chain not "
                "exact; the native crosshair stays visible");
        LOG("Reach FP camera hook installed for the exact HREK-homologous "
            "nested workspace; per-eye execution pending");
        LOG("Reach comfort evidence: blurScale=%llX blurMax=%llX "
            "(authored %.4f/%.4f); VR default keeps scale finite and zeros max",
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(reachMotionBlurScale) - base),
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(reachMotionBlurMax) - base),
            reachMotionBlurScaleOriginal, reachMotionBlurMaxOriginal);
        LOG("Reach stereo fog evidence: screen-aligned patchy helper %llX "
            "is suppressed per eye through exact flag %llX bit 0x%02X",
            static_cast<unsigned long long>(kReachPatchyFogTargetRva),
            static_cast<unsigned long long>(kReachPatchyFogFlagsRva), static_cast<unsigned>(kReachPatchyFogSkipMask));
        LOG("Reach per-eye outer camera commit BOUND with zero samples: exact "
            "native callback +0x%llX will rebind each rebuilt eye before "
            "player_view_render",
            static_cast<unsigned long long>(kReachCameraStackCallbackRva));

        return true;
    }

    void LogReachFpCameraUploadIfReady()
    {
        const uint64_t serialBefore = g_reachFpCameraUploadStatus.preparedSerial.load(std::memory_order_acquire);
        if (!serialBefore)
            return;
        const uint32_t generation = g_reachFpCameraUploadStatus.generation.load(std::memory_order_relaxed);
        const uint32_t eyeMask = g_reachFpCameraUploadStatus.eyeMask.load(std::memory_order_acquire);
        const uint64_t serialAfter = g_reachFpCameraUploadStatus.preparedSerial.load(std::memory_order_acquire);
        if (serialBefore != serialAfter || generation != g_reachCamera.generation || (eyeMask & 0x3u) != 0x3u ||
            generation == g_reachFpCameraLoggedGeneration.load(std::memory_order_relaxed))
        {
            return;
        }
        g_reachFpCameraLoggedGeneration.store(generation, std::memory_order_release);
        LOG("Reach per-eye FP camera ACTIVE: both eyes uploaded world compact "
            "+ derived projection through the dedicated nested workspace "
            "(viewmodel depth uncrushed)");
    }

    void LogReachOuterCameraCommitIfReady()
    {
        const uint64_t serialBefore = g_reachOuterCameraCommitStatus.preparedSerial.load(std::memory_order_acquire);
        if (!serialBefore)
            return;
        const uint32_t generation = g_reachOuterCameraCommitStatus.generation.load(std::memory_order_relaxed);
        const uint32_t eyeMask = g_reachOuterCameraCommitStatus.eyeMask.load(std::memory_order_acquire);
        const uint64_t serialAfter = g_reachOuterCameraCommitStatus.preparedSerial.load(std::memory_order_acquire);
        if (serialBefore != serialAfter || generation != g_reachCamera.generation || (eyeMask & 0x3u) != 0x3u ||
            generation == g_reachOuterCameraCommitLoggedGeneration.load(std::memory_order_relaxed))
        {
            return;
        }
        g_reachOuterCameraCommitLoggedGeneration.store(generation, std::memory_order_release);
        LOG("Reach per-eye outer camera commit ACTIVE: both rebuilt eyes "
            "committed through the native camera-stack callback before "
            "player_view_render");
    }

    void LogReachFpStatusIfNew()
    {
        static uint32_t legLogGeneration = 0;
        static bool loggedLegObservation = false;
        static bool loggedLegPreserved = false;
        static bool loggedLegIdentityReject = false;
        static bool loggedLegBuildFailure = false;
        static uint64_t reportedFingerApplied = 0;
        static uint64_t reportedFingerRefused = 0;
        static uint64_t reportedFingerWrites = 0;
        static uint64_t reportedFingerRollback = 0;
        const uint32_t currentGeneration = g_reachCamera.generation.load(std::memory_order_acquire);
        if (currentGeneration != legLogGeneration)
        {
            legLogGeneration = currentGeneration;
            loggedLegObservation = false;
            loggedLegPreserved = false;
            loggedLegIdentityReject = false;
            loggedLegBuildFailure = false;
            reportedFingerApplied = g_reachFingerPoseApplied.load(std::memory_order_relaxed);
            reportedFingerRefused = g_reachFingerPoseRefused.load(std::memory_order_relaxed);
            reportedFingerWrites = g_reachFingerPoseWriteFailures.load(std::memory_order_relaxed);
            reportedFingerRollback = g_reachFingerPoseRollbackFailures.load(std::memory_order_relaxed);
        }
        const uint32_t legObservations = g_reachFpLegPaletteObservationCount.load(std::memory_order_relaxed);
        const uint32_t legPreserved = g_reachFpLegPalettePreserved.load(std::memory_order_relaxed);
        const uint32_t legRejects = g_reachFpLegPaletteIdentityRejects.load(std::memory_order_relaxed);
        const uint32_t legBuildFailures = g_reachFpLegPaletteBuildFailures.load(std::memory_order_relaxed);
        if (!loggedLegObservation && legObservations)
        {
            loggedLegObservation = true;
            LOG("Reach FP seated legs: 82/67-node palette candidate observed "
                "(count=%u); exact HREK checksum verification runs at the "
                "next prepared pair",
                legObservations);
        }
        if (!loggedLegPreserved && legPreserved)
        {
            loggedLegPreserved = true;
            LOG("Reach FP seated legs ACTIVE: exact native fp_body palette "
                "preserved in the occupied first-person seat (palettes=%u); "
                "controller hands remain separate; the independent world-biped "
                "hide transaction is unchanged",
                legPreserved);
        }
        if (!loggedLegIdentityReject && legRejects)
        {
            loggedLegIdentityReject = true;
            LOG("Reach FP seated legs: a count candidate did not match the "
                "exact native fp_body checksum/tag identity (rejected=%u)",
                legRejects);
        }
        if (!loggedLegBuildFailure && legBuildFailures)
        {
            loggedLegBuildFailure = true;
            LOG("Reach FP seated legs STOCK FOR THIS PALETTE: centered native "
                "body conversion failed (failures=%u); floating hands and the "
                "shared FP layout remain active",
                legBuildFailures);
        }

        const uint64_t fingerApplied = g_reachFingerPoseApplied.load(std::memory_order_relaxed);
        const uint64_t fingerRefused = g_reachFingerPoseRefused.load(std::memory_order_relaxed);
        const uint64_t fingerWrites = g_reachFingerPoseWriteFailures.load(std::memory_order_relaxed);
        const uint64_t fingerRollback = g_reachFingerPoseRollbackFailures.load(std::memory_order_relaxed);
        if (fingerApplied > reportedFingerApplied || fingerRefused > reportedFingerRefused ||
            fingerWrites > reportedFingerWrites || fingerRollback > reportedFingerRollback)
        {
            LOG("Reach FP free-support finger pose: applied=%llu refused=%llu write_failures=%llu rollback_failures=%llu; stock palette remains the fallback",
                static_cast<unsigned long long>(fingerApplied), static_cast<unsigned long long>(fingerRefused),
                static_cast<unsigned long long>(fingerWrites), static_cast<unsigned long long>(fingerRollback));
            reportedFingerApplied = fingerApplied;
            reportedFingerRefused = fingerRefused;
            reportedFingerWrites = fingerWrites;
            reportedFingerRollback = fingerRollback;
        }

        const uint64_t key = g_reachFpStatus.key.load(std::memory_order_acquire);
        if (!key || key == g_reachFpLoggedStatusKey.load(std::memory_order_relaxed))
            return;
        const uint32_t generation = g_reachFpStatus.generation.load(std::memory_order_relaxed);
        const int code = g_reachFpStatus.code.load(std::memory_order_relaxed);
        const int bodyCount = g_reachFpStatus.bodyCount.load(std::memory_order_relaxed);
        const int liveCount = g_reachFpStatus.liveCount.load(std::memory_order_relaxed);
        if (generation != g_reachCamera.generation)
            return;
        g_reachFpLoggedStatusKey.store(key, std::memory_order_release);
        if (code == 1)
            LOG("Reach FP layout learned stock-only: body=%d live=%d; eligible next prepared pair", bodyCount,
                liveCount);
        else if (code == 2)
            LOG("Reach FP centered native lower-body palette committed: body=%d live=%d; "
                "floating_hands=%d arm_ik=%d; support-grip arm endpoint follows "
                "the authored weapon delta when that transaction commits",
                bodyCount, liveCount, static_cast<int>(g_config.floating_hands), static_cast<int>(g_config.arm_ik));
        else if (code == 3)
            LOG("Reach FP layout changed or failed validation at live=%d; next pair remains stock", liveCount);
    }

    void ReachVehicleInputLogTick()
    {
        static uint64_t loggedSnapshot = 0;
        static uint32_t loggedFaultGeneration = 0;
        static uint32_t cameraLogGeneration = 0;
        static bool loggedCameraFault = false;
        static bool loggedIdentityMiss = false;
        static bool loggedMarkerFailure = false;
        static bool loggedBoundsFailure = false;
        static bool loggedBounceFallback = false;
        static bool loggedFollowFallback = false;
        static bool loggedUnitCameraHide = false;
        static bool loggedUnitCameraHideFailure = false;
        static uint32_t loggedPatchSerial = 0;
        static uint32_t loggedPatchState = 0;
        static bool loggedPatchFailure = false;
        static bool loggedShotMove = false;
        static uint32_t loggedShotSkip = 0;
        static bool loggedVehicleShotRedirect = false;
        static bool loggedVehicleShotMiss = false;
        static bool loggedPassengerFirstPersonAdmission = false;
        static bool loggedAnchorFallback = false;
        static bool loggedNativeAim = false;
        static bool loggedAimFallback = false;
        static bool loggedAimReadFallback = false;
        static bool loggedReticleAimFallback = false;
        static uint32_t loggedRecenters = 0;
        static uint32_t loggedExitRecenters = 0;
        static uint64_t loggedSeat = 0;
        const uint32_t generation = g_reachCamera.generation.load(std::memory_order_acquire);
        if (!generation)
            return;
        if (cameraLogGeneration != generation)
        {
            cameraLogGeneration = generation;
            loggedCameraFault = false;
            loggedIdentityMiss = false;
            loggedMarkerFailure = false;
            loggedBoundsFailure = false;
            loggedBounceFallback = false;
            loggedFollowFallback = false;
            loggedUnitCameraHide = false;
            loggedUnitCameraHideFailure = false;
            loggedPatchSerial = 0;
            loggedPatchState = 0;
            loggedPatchFailure = false;
            loggedRecenters = 0;
            loggedExitRecenters = 0;
            loggedSeat = 0;
            loggedShotMove = false;
            loggedShotSkip = 0;
            loggedVehicleShotRedirect = false;
            loggedVehicleShotMiss = false;
            loggedPassengerFirstPersonAdmission = false;
            loggedAnchorFallback = false;
            loggedNativeAim = false;
            loggedAimFallback = false;
            loggedAimReadFallback = false;
            loggedReticleAimFallback = false;
            for (auto& missSlot : g_reachVehicleCensusMisses)
                missSlot.state.store(0, std::memory_order_release);
        }
        if (!loggedNativeAim && g_reachUnitAimSamples.load(std::memory_order_relaxed) != 0)
        {
            loggedNativeAim = true;
            LOG("Reach vehicle aim: native unit+0x214 feedback active for "
                "input steering with view follow OFF or ON; the visible sight "
                "and projectile central line use the controller reticle");
        }
        if (!loggedAimFallback && g_reachUnitAimFallbacks.load(std::memory_order_relaxed) != 0)
        {
            loggedAimFallback = true;
            LOG("Reach vehicle aim: native unit aim was unavailable for at "
                "least one seated frame; compact-camera fallback kept that "
                "frame live");
        }
        if (!loggedAimReadFallback && g_reachAimFeedbackReadFallbacks.load(std::memory_order_relaxed) != 0)
        {
            loggedAimReadFallback = true;
            LOG("Reach vehicle aim: coherent feedback read was stale or torn; "
                "the input frame used the legacy compact publication");
        }
        if (!loggedReticleAimFallback && g_reachReticleAimFallbacks.load(std::memory_order_relaxed) != 0)
        {
            loggedReticleAimFallback = true;
            LOG("Reach vehicle reticle: native seated aim was unavailable or "
                "could not map into tracked space; controller reticle retained");
        }
        if (!loggedShotMove && g_reachFiringOriginMoves.load(std::memory_order_relaxed) != 0)
        {
            loggedShotMove = true;
            LOG("Reach seat aim: seated personal-weapon shots are leaving the "
                "rendered eye (first substitution observed)");
        }
        // Re-logged whenever a NEW reason first appears, not once per
        // generation: the breakdown is the whole point and the first skip
        // alone would report four zeroes forever. At most five lines.
        const uint32_t shotSkipMask = (g_reachFiringOriginSkipTorn.load(std::memory_order_relaxed) ? 1u : 0u) |
                                      (g_reachFiringOriginSkipNotSeated.load(std::memory_order_relaxed) ? 2u : 0u) |
                                      (g_reachFiringOriginSkipNoWeaponSeat.load(std::memory_order_relaxed) ? 4u : 0u) |
                                      (g_reachFiringOriginSkipKeyMismatch.load(std::memory_order_relaxed) ? 8u : 0u) |
                                      (g_reachFiringOriginSkipStaleEye.load(std::memory_order_relaxed) ? 16u : 0u) |
                                      (g_reachFiringOriginSkipNoSightRay.load(std::memory_order_relaxed) ? 32u : 0u);
        if (shotSkipMask != loggedShotSkip && g_reachFiringOriginSkips.load(std::memory_order_relaxed) != 0)
        {
            loggedShotSkip = shotSkipMask;
            LOG("Reach seat aim: %u firing-path substitutions were SKIPPED and "
                "kept the stock origin - torn read %u, not seated %u, seat "
                "does not allow a personal weapon %u, occupation/eye key "
                "mismatch %u, stale or invalid rendered eye %u, no fresh "
                "presented sight ray %u",
                g_reachFiringOriginSkips.load(std::memory_order_relaxed),
                g_reachFiringOriginSkipTorn.load(std::memory_order_relaxed),
                g_reachFiringOriginSkipNotSeated.load(std::memory_order_relaxed),
                g_reachFiringOriginSkipNoWeaponSeat.load(std::memory_order_relaxed),
                g_reachFiringOriginSkipKeyMismatch.load(std::memory_order_relaxed),
                g_reachFiringOriginSkipStaleEye.load(std::memory_order_relaxed),
                g_reachFiringOriginSkipNoSightRay.load(std::memory_order_relaxed));
        }
        if (!loggedVehicleShotRedirect && g_reachVehicleShotRedirects.load(std::memory_order_relaxed) != 0)
        {
            loggedVehicleShotRedirect = true;
            LOG("Reach vehicle shot line: native selected-barrel central "
                "direction redirected through the presented stabilized VR "
                "reticle (first local shot; stock spread/ballistics retained)");
        }
        if (!loggedVehicleShotMiss && g_reachVehicleShotRedirectMisses.load(std::memory_order_relaxed) != 0)
        {
            loggedVehicleShotMiss = true;
            LOG("Reach vehicle shot line: a local seated shot lacked a fresh "
                "exact-key presented-reticle target; that shot remained stock");
        }
        if (!loggedPassengerFirstPersonAdmission &&
            g_reachPassengerFirstPersonAdmissions.load(std::memory_order_relaxed) != 0)
        {
            loggedPassengerFirstPersonAdmission = true;
            LOG("Reach passenger floating hands ACTIVE: the exact "
                "render_first_person_view predicate admitted the native "
                "personal hands/gun before interpolation and palette; no seat "
                "flag, camera, steering or simulation state was changed");
        }
        if (!loggedCameraFault && g_reachVehicleCameraFaults.load(std::memory_order_relaxed) != 0)
        {
            loggedCameraFault = true;
            LOG("Reach first-person vehicles: engine-thread camera sample "
                "faulted; only the optional vehicle transaction is now stock, "
                "the Reach stereo camera core remains armed");
        }
        if (!loggedIdentityMiss && g_reachVehicleIdentityMisses.load(std::memory_order_relaxed) != 0)
        {
            loggedIdentityMiss = true;
            LOG("Reach first-person vehicles: an occupied seat failed the "
                "structural layout proofs (object kind, vehi tag, seat block "
                "or seat-camera pointer); that seat remains stock");
        }
        for (auto& missSlot : g_reachVehicleCensusMisses)
        {
            uint32_t expected = 2;
            if (!missSlot.state.compare_exchange_strong(expected, 3, std::memory_order_acq_rel))
                continue;
            if (missSlot.knownIdentity != 0 && missSlot.knownIdentity <= kReachVehicleTrimCount)
            {
                LOG("Reach first-person vehicles: %s raw seat %d (of %d "
                    "authored) is outside the proven player-seat census; the "
                    "seat camera is applied with that seat's own per-seat "
                    "trims",
                    kReachVehicleTrimNames[missSlot.knownIdentity - 1], missSlot.seatIndex, missSlot.seatCount);
            }
            else
            {
                LOG("Reach first-person vehicles: unmatched vehicle tuple "
                    "%08X/%08X/%08X/%08X type %d raw seat %d (of %d authored); "
                    "the universal seat camera is applied with no-follow/"
                    "no-steer policy",
                    missSlot.fingerprint.radius, missSlot.fingerprint.offsetX, missSlot.fingerprint.offsetY,
                    missSlot.fingerprint.offsetZ, missSlot.observedType, missSlot.seatIndex, missSlot.seatCount);
            }
        }
        if (!loggedMarkerFailure && g_reachVehicleMarkerFailures.load(std::memory_order_relaxed) != 0)
        {
            loggedMarkerFailure = true;
            LOG("Reach first-person vehicles: seat/root marker resolution "
                "failed; that frame remained stock");
        }
        if (!loggedBoundsFailure && g_reachVehicleBoundsFailures.load(std::memory_order_relaxed) != 0)
        {
            loggedBoundsFailure = true;
            LOG("Reach first-person vehicles: a finite seat/config point fell "
                "outside the vehicle bounds guard; that frame remained stock");
        }
        if (!loggedBounceFallback && g_reachVehicleBounceFallbacks.load(std::memory_order_relaxed) != 0)
        {
            loggedBounceFallback = true;
            LOG("Reach first-person vehicles: occupant head-marker bounce "
                "could not be proven; rigid authored seat placement continues");
        }
        if (!loggedAnchorFallback && g_reachVehicleAnchorFallbacks.load(std::memory_order_relaxed) != 0)
        {
            loggedAnchorFallback = true;
            LOG("Reach first-person vehicles: rigid seat parenting could not "
                "be proven for at least one frame; the animated marker anchor "
                "was used for those frames");
        }
        if (!loggedFollowFallback && g_reachVehicleFollowFallbacks.load(std::memory_order_relaxed) != 0)
        {
            loggedFollowFallback = true;
            LOG("Reach first-person vehicles: attached-weapon carrier root "
                "could not be resolved; camera placement continues but view "
                "follow is stock/world-locked for that seat");
        }
        const uint32_t unitCameraHideSets = g_reachUnitCameraHideSets.load(std::memory_order_relaxed);
        const uint32_t unitCameraHideNative = g_reachUnitCameraHideAlreadySet.load(std::memory_order_relaxed);
        if (!loggedUnitCameraHide && (unitCameraHideSets != 0 || unitCameraHideNative != 0))
        {
            loggedUnitCameraHide = true;
            LOG("Reach first-person vehicles: native unit-camera hide-player "
                "bit 0x%04X observed inside the admitted outer render "
                "(set=%u, already=%u, restored=%u)",
                static_cast<unsigned>(kReachUnitCameraHidePlayerBit), unitCameraHideSets, unitCameraHideNative,
                g_reachUnitCameraHideRestores.load(std::memory_order_relaxed));
        }
        if (!loggedUnitCameraHideFailure && g_reachUnitCameraHideFailures.load(std::memory_order_relaxed) != 0)
        {
            loggedUnitCameraHideFailure = true;
            LOG("Reach first-person vehicles: native unit-camera body hide "
                "could not be set or restored exactly; body hide alone fell "
                "back for that frame and the seat camera continued");
        }
        // Report what the ENGINE holds for the seat, once per change of
        // state, not that we wrote it. `active` means the occupied seat is
        // reporting first person right now, which is the state Halo 3 testing
        // proved brings up the occupant's own arms and weapon;
        // `external/native` means the seat was already first person (the
        // Workshop first-person-vehicle maps do this in the tags) and we own
        // no write at all. Either of those is a seat that should show hands.
        const uint32_t patchSerial = g_reachVehicleSeatPatchSerial.load(std::memory_order_acquire);
        const ReachSeatLeaseState leaseNow = ReachNativeSeatLeaseState();
        if (patchSerial != 0 &&
            (patchSerial != loggedPatchSerial || static_cast<uint32_t>(leaseNow) != loggedPatchState))
        {
            loggedPatchSerial = patchSerial;
            loggedPatchState = static_cast<uint32_t>(leaseNow);
            LOG("Reach seat first person: the occupied seat's third-person "
                "camera bit is %s (serial=%u, state=%s); the engine's own "
                "first-person arms/weapon transaction has run %u times while "
                "seated this session",
                (leaseNow == ReachSeatLeaseState::Active)
                    ? "CLEARED by us for this occupation"
                    : (leaseNow == ReachSeatLeaseState::External ? "already clear in the loaded tag; we own no write"
                                                                 : "not currently owned"),
                patchSerial, ReachNativeSeatLeaseStateName(leaseNow),
                g_reachSeatedFpPaletteCalls.load(std::memory_order_relaxed));
        }
        if (!loggedPatchFailure && g_reachVehicleSeatPatchFailures.load(std::memory_order_relaxed) != 0)
        {
            loggedPatchFailure = true;
            LOG("Reach seat first person: the seat's camera bit could not be "
                "cleared or restored safely; that seat keeps its stock "
                "third-person presentation while unit-aim feedback and the "
                "seat camera continue");
        }
        const uint32_t recenters = g_reachVehicleSeatRecenters.load(std::memory_order_relaxed);
        if (recenters != loggedRecenters)
        {
            LOG("Reach first-person vehicles: full play-space recentered on "
                "exact occupied-seat entry (count=%u; View Follow independent)",
                recenters);
            loggedRecenters = recenters;
        }
        const uint32_t exitRecenters = g_reachVehicleExitRecenters.load(std::memory_order_relaxed);
        if (exitRecenters != loggedExitRecenters)
        {
            LOG("Reach first-person vehicles: position-only reference "
                "recentered on settled vehicle exit (count=%u)",
                exitRecenters);
            loggedExitRecenters = exitRecenters;
        }
        const uint64_t seatSnapshot = g_reachVehicleTrimSnapshot.load(std::memory_order_acquire);
        if (seatSnapshot != loggedSeat && static_cast<uint32_t>(seatSnapshot >> 32) == generation)
        {
            loggedSeat = seatSnapshot;
            const int identity = static_cast<int>((seatSnapshot >> 8) & 0xFFu);
            const int seat = static_cast<int>(seatSnapshot & 0xFFu) - 1;
            const int slot = ReachVehicleTrimSnapshotSlot(seatSnapshot, generation, kReachVehicleSeatSlots);
            // The live seat-flags word is printed because two features gate on
            // it (bit 4 third person camera, bit 5 allows weapons) and a
            // report of "no passenger hands" cannot be told apart from "this
            // seat never claimed to allow weapons" without it.
            const uint32_t seatFlagsNow = g_reachVehicleSeatFlagsPublished.load(std::memory_order_acquire);
            if (identity > 0 && identity <= kReachVehicleIdentityTrimCount && seat >= 0 && slot >= 0)
            {
                LOG("Reach first-person vehicles: active %s raw seat %d; "
                    "marker smoothing=%d trims F/R/U=%.2f/%.2f/%.2f m; "
                    "seat flags %08X (third-person=%d allows-weapons=%d); "
                    "view follow %s, %s",
                    kReachVehicleTrimNames[identity - 1], seat, static_cast<int>(g_config.vehicle_cam_smoothing),
                    ConfigReachSeatCamForward(g_config, slot), ConfigReachSeatCamRight(g_config, slot),
                    ConfigReachSeatCamUp(g_config, slot), seatFlagsNow,
                    (seatFlagsNow & kReachSeatThirdPersonCameraBit) ? 1 : 0,
                    (seatFlagsNow & kReachSeatAllowsWeaponsBit) ? 1 : 0, g_config.vehicle_view_follow ? "ON" : "OFF",
                    ReachSteeringModeName(static_cast<ReachVehicleId>(identity), seat));
            }
            else if (identity == kReachVehicleGenericIdentityCode && seat >= 0 && slot >= 0)
            {
                LOG("Reach first-person vehicles: active unmatched vehicle raw "
                    "seat %d; marker smoothing=%d unmatched-row trims "
                    "F/R/U=%.2f/%.2f/%.2f m; seat flags %08X "
                    "(third-person=%d allows-weapons=%d); the F1 sliders edit "
                    "that row, never the shared universal trim",
                    seat, static_cast<int>(g_config.vehicle_cam_smoothing), ConfigReachSeatCamForward(g_config, slot),
                    ConfigReachSeatCamRight(g_config, slot), ConfigReachSeatCamUp(g_config, slot), seatFlagsNow,
                    (seatFlagsNow & kReachSeatThirdPersonCameraBit) ? 1 : 0,
                    (seatFlagsNow & kReachSeatAllowsWeaponsBit) ? 1 : 0);
            }
        }
        if (g_reachVehicleInputFaults.load(std::memory_order_relaxed) != 0 && loggedFaultGeneration != generation)
        {
            loggedFaultGeneration = generation;
            LOG("Reach vehicle input: runtime title-call fault; only this "
                "optional refinement fell back to the existing Reach-wide "
                "LT/X swap, and the camera core continues");
        }

        const uint64_t snapshot = g_reachVehicleInputSnapshot.load(std::memory_order_acquire);
        if (static_cast<uint32_t>(snapshot >> 32) != generation || snapshot == loggedSnapshot)
            return;
        const auto state = static_cast<ReachVehicleInputState>(static_cast<uint32_t>(snapshot));
        if (state == ReachVehicleInputState::Unknown)
            return;
        loggedSnapshot = snapshot;
        if (state == ReachVehicleInputState::Vehicle)
        {
            LOG("Reach vehicle input: local player seated; native LT/X active "
                "for this vehicle");
        }
        else
        {
            LOG("Reach vehicle input: local player on foot; armour/grenade "
                "LT/X swap active");
        }
    }

    // Called from the 50 ms title worker's Reach block. Self-contained: it never
    // touches the Halo 3 or ODST state machines.
    // Parity with PublishOdstLifecycle above. Without this Reach never reports
    // an armed lifecycle, so TitleRuntimeMaskUnarmedCapabilities strips every
    // arm-gated capability -- including ControllerAim -- and shared features
    // that ask Game_HasTitleCapability (the VR crosshair quad) stay refused
    // even while Reach's camera core is fully armed and rendering stereo.
    bool PublishReachLifecycle()
    {
        const uint32_t generation = TitleAdapter_GetGeneration(GameTitle::HaloReach);
        if (!generation)
            return false;
        TitleRuntimeLifecycle lifecycle{};
        lifecycle.installed = g_reachCamera.installed.load(std::memory_order_acquire);
        lifecycle.armed = g_reachCamera.armed.load(std::memory_order_acquire);
        lifecycle.teardownRequested = g_reachCamera.teardownRequested.load(std::memory_order_acquire);
        lifecycle.enabledCapabilities =
            lifecycle.installed && !lifecycle.teardownRequested ? kReachRuntimeCapabilities : TitleCapability_None;
        if (!g_reachCineProbe.armed.load(std::memory_order_acquire))
            lifecycle.enabledCapabilities &= ~TitleCapability_CutsceneTheater;

        // Publish ONLY on a real state change. Republishing identical state
        // every 50 ms poll re-opens the runtime publication sequence
        // constantly, which keeps the shared snapshot "pending". A pending
        // snapshot reports mode=Loading with NO capabilities
        // (PendingSnapshotFromCandidate), so Game_HasTitleCapability kept
        // denying ControllerAim and the VR reticle chain was never created --
        // visible in the log as "Runtime mode: gameplay -> loading" flapping
        // ~10x/second forever.
        static uint32_t publishedGeneration = 0;
        static uint32_t publishedState = 0xFFFFFFFFu;
        const uint32_t state = (lifecycle.installed ? 1u : 0u) | (lifecycle.armed ? 2u : 0u) |
                               (lifecycle.teardownRequested ? 4u : 0u) | (lifecycle.enabledCapabilities << 3);
        if (generation == publishedGeneration && state == publishedState)
            return true;
        const bool published = TitleAdapter_PublishLifecycle(GameTitle::HaloReach, generation, lifecycle);
        if (published)
        {
            publishedGeneration = generation;
            publishedState = state;
        }
        return published;
    }
    // Worker-side only. This reports both the unconditional zero-sample armed
    // state and the first real eye suppression immediately. Later movement is
    // rate-limited to 30 seconds. The hot render hook only updates atomics.
    void ReachLightmapShadowLogTick()
    {
        static uint32_t lastGeneration = 0;
        static uint32_t lastTotal = 0;
        static uint64_t lastReportMs = 0;
        if (!g_reachCamera.installed.load(std::memory_order_acquire))
        {
            lastGeneration = 0;
            lastTotal = 0;
            lastReportMs = 0;
            return;
        }
        if (!g_reachCamera.armed.load(std::memory_order_acquire))
            return;
        if (!g_reachCamera.lightmapShadowsEnabled)
            return;

        const uint32_t generation = g_reachCamera.generation;
        const uint32_t suppressed = g_reachLightmapShadowSuppressedEyes.load(std::memory_order_relaxed);
        const uint32_t alreadyDisabled = g_reachLightmapShadowAlreadyDisabledEyes.load(std::memory_order_relaxed);
        const uint32_t writeFailures = g_reachLightmapShadowWriteFailures.load(std::memory_order_relaxed);
        const uint32_t restoreFailures = g_reachLightmapShadowRestoreFailures.load(std::memory_order_relaxed);
        const uint32_t total = suppressed + alreadyDisabled + writeFailures + restoreFailures;
        const uint64_t now = GetTickCount64();
        const bool newGeneration = generation != lastGeneration;
        const bool firstActivity = total != 0 && lastTotal == 0;
        const bool periodic = total != lastTotal && lastReportMs != 0 && now - lastReportMs >= 30000;
        if (!newGeneration && !firstActivity && !periodic)
            return;

        lastGeneration = generation;
        lastTotal = total;
        lastReportMs = now;
        LOG("Reach world-shadow candidate: generation=%u, "
            "suppressed-eye-renders=%u, already-disabled-eye-renders=%u, "
            "write-failures=%u, restore-failures=%u",
            generation, suppressed, alreadyDisabled, writeFailures, restoreFailures);
    }

    // Worker-side only; the hot path just bumps atomics. This reports on a
    // plain 10-second period rather than only when the numbers move, because
    // the observer-camera site table taught the opposite lesson: a report that
    // prints only on change printed once, 57 ms after arming, and could never
    // show whether the behavior was working. A headset result is only usable
    // beside the achieved counts.
    void ReachWeatherLogTick()
    {
        static uint64_t lastReportMs = 0;
        if (!g_reachCamera.installed.load(std::memory_order_acquire) ||
            !g_reachCamera.armed.load(std::memory_order_acquire))
        {
            lastReportMs = 0;
            return;
        }
        if (!g_reachCamera.renderRainSlot && !g_reachCamera.atmosphereFogTlsIndex && !g_reachCamera.decoratorWindState)
        {
            return; // Neither control was proven; nothing to report.
        }
        const uint64_t now = GetTickCount64();
        if (lastReportMs != 0 && now - lastReportMs < 10000)
            return;
        lastReportMs = now;
        LOG("Reach decorator wind: replay-pairs=%u faults=%u state=%s",
            g_reachWindReplayPairs.load(std::memory_order_relaxed),
            g_reachWindReplayFaults.load(std::memory_order_relaxed),
            g_reachCamera.decoratorWindState && !g_reachWindReplayFaults.load(std::memory_order_relaxed)
                ? "Installed"
                : "StockFallback");
        LOG("Reach weather: rain %s (setting %s, held-off eyes=%u), "
            "atmospheric fog %s (setting %s, held-off eyes=%u), "
            "flags-unresolved=%u, write-failures=%u",
            g_reachCamera.renderRainSlot ? "bound" : "stock", g_config.rain ? "on" : "off",
            g_reachRainSuppressedEyes.exchange(0, std::memory_order_relaxed),
            g_reachCamera.atmosphereFogTlsIndex ? "bound" : "stock", g_config.atmospheric_fog ? "on" : "off",
            g_reachFogSuppressedEyes.exchange(0, std::memory_order_relaxed),
            g_reachFogFlagsUnresolved.exchange(0, std::memory_order_relaxed),
            g_reachWeatherWriteFailures.load(std::memory_order_relaxed));
    }

    // Worker-side proof that the behavioral candidate actually executed. The
    // native render detour performs no logging, allocation, or locking; it only
    // increments lock-free counters for the exact owned eye or stock pass-through.
    void ReachSsaoLogTick()
    {
        static uint32_t lastGeneration = 0;
        static uint32_t lastSuppressed = 0;
        static uint32_t lastPassthrough = 0;
        static uint64_t lastReportMs = 0;
        if (!g_reachCamera.installed.load(std::memory_order_acquire) ||
            !g_reachCamera.ssaoIsolationActive.load(std::memory_order_acquire))
        {
            lastGeneration = 0;
            lastSuppressed = 0;
            lastPassthrough = 0;
            lastReportMs = 0;
            return;
        }
        if (!g_reachCamera.armed.load(std::memory_order_acquire))
            return;

        const uint32_t generation = g_reachCamera.generation;
        const uint32_t eye0 = g_reachSsaoSuppressedEyeCalls[0].load(std::memory_order_relaxed);
        const uint32_t eye1 = g_reachSsaoSuppressedEyeCalls[1].load(std::memory_order_relaxed);
        const uint32_t suppressed = eye0 + eye1;
        const uint32_t passthrough = g_reachSsaoPassthroughCalls.load(std::memory_order_relaxed);
        const uint64_t now = GetTickCount64();
        const bool newGeneration = generation != lastGeneration;
        const bool firstSuppression = suppressed != 0 && lastSuppressed == 0;
        const bool periodic = (suppressed != lastSuppressed || passthrough != lastPassthrough) && lastReportMs != 0 &&
                              now - lastReportMs >= 30000;
        if (!newGeneration && !firstSuppression && !periodic)
            return;

        lastGeneration = generation;
        lastSuppressed = suppressed;
        lastPassthrough = passthrough;
        lastReportMs = now;
        LOG("Reach SSAO candidate EXECUTION: generation=%u, suppressed eye0=%u "
            "eye1=%u, stock pass-through calls=%u",
            generation, eye0, eye1, passthrough);
    }

    // Worker-side only. Reports what the effect-location hook actually saw, so
    // one short headset run decides the muzzle mechanism instead of a fourth
    // guess. Silent until something happens, then rate-limited to 30 s.
    void ReachMuzzleLogTick()
    {
        static uint32_t lastTotal = 0;
        static uint64_t lastReportMs = 0;
        const uint32_t redirects = g_reachMuzzleRedirects.load(std::memory_order_relaxed);
        const uint32_t noFpUser = g_reachMuzzleWorldNoFpUser.load(std::memory_order_relaxed);
        const uint32_t fpNone = g_reachMuzzleWorldFpNone.load(std::memory_order_relaxed);
        const uint32_t alreadyFp = g_reachMuzzleAlreadyFp.load(std::memory_order_relaxed);
        const uint32_t failures = g_reachMuzzleReadFailures.load(std::memory_order_relaxed);
        const uint32_t total = redirects + noFpUser + fpNone + alreadyFp + failures +
                               g_reachMuzzleReparented.load(std::memory_order_relaxed) +
                               g_reachMuzzleOutOfRange.load(std::memory_order_relaxed);
        if (!total || total == lastTotal)
            return;
        const uint64_t now = GetTickCount64();
        if (lastReportMs && now - lastReportMs < 30000)
            return;
        lastReportMs = now;
        lastTotal = total;
        LOG("REACHFX: retargeted=%u; HUD flash widgets hidden %u; live-graph weapon writes %u; "
            "muzzle re-parented %u, "
            "out of range %u, "
            "nearest approach %u mm; lined up %u (no sibling yet %u); "
            "effect locations - redirected %u, world/no-fp-user %u, "
            "world/fp-output-none %u, already-first-person %u, unreadable %u "
            "(effect+0x50 low nibbles seen 0x%04X, high nibbles 0x%04X)",
            g_reachMuzzleRetargetDone.load(std::memory_order_relaxed),
            g_reachHudFlashHidden.load(std::memory_order_relaxed),
            g_reachLiveGraphWeaponWrites.load(std::memory_order_relaxed),
            g_reachMuzzleReparented.load(std::memory_order_relaxed),
            g_reachMuzzleOutOfRange.load(std::memory_order_relaxed),
            g_reachMuzzleNearestMilli.load(std::memory_order_relaxed),
            g_reachMuzzleRepaired.load(std::memory_order_relaxed),
            g_reachMuzzleIdentityNoSibling.load(std::memory_order_relaxed), redirects, noFpUser, fpNone, alreadyFp,
            failures, g_reachMuzzleFpByteLowMask.load(std::memory_order_relaxed),
            g_reachMuzzleFpByteHighMask.load(std::memory_order_relaxed));
    }

    // Worker-side proof that the rejected world-wrist writes stay prevented. The palette hook
    // only increments a lock-free counter; logging stays off the render path.
    void ReachWorldWristWriteLogTick()
    {
        static uint32_t lastGeneration = 0;
        static uint32_t lastPrevented = 0;
        static uint64_t lastReportMs = 0;
        if (!g_reachCamera.installed.load(std::memory_order_acquire))
        {
            lastGeneration = 0;
            lastPrevented = 0;
            lastReportMs = 0;
            return;
        }
        const uint32_t generation = g_reachCamera.generation;
        const uint32_t prevented = g_reachWorldWristWritesPrevented.load(std::memory_order_relaxed);
        const uint32_t executed = g_reachLiveGraphWeaponWrites.load(std::memory_order_relaxed);
        const uint64_t now = GetTickCount64();
        const bool newGeneration = generation != lastGeneration;
        const bool firstPrevention = prevented != 0 && lastPrevented == 0;
        const bool periodic = prevented != lastPrevented && lastReportMs != 0 && now - lastReportMs >= 30000;
        if (!newGeneration && !firstPrevention && !periodic)
            return;
        lastGeneration = generation;
        lastPrevented = prevented;
        lastReportMs = now;
        LOG("Reach FP post-palette world-to-local wrist write DISABLED: "
            "generation=%u, prevented=%u, executed=%u",
            generation, prevented, executed);
    }

    // Worker-side only: report which of the six observer-camera call sites are
    // actually exercised, and which ones the head-lock corrected.
    //
    // This originally logged only when the SET of exercised sites changed, which
    // made it nearly useless: an accepted headset session printed one snapshot
    // 57 ms after arming, with 0 corrections on every row because the eye scope
    // had not run yet, and never printed again. The headset result proved the
    // correction works while the counters said nothing. Report on a set change
    // OR every 30 s while counts are still moving, so the numbers reflect play.
    void ReachObserverCameraLogTick()
    {
        static uint32_t lastMask = 0xFFFFFFFFu;
        static uint64_t lastReportMs = 0;
        static uint32_t lastTotal = 0;
        uint32_t mask = 0;
        uint32_t hits[6]{};
        uint32_t fixed[6]{};
        uint32_t total = 0;
        for (int site = 0; site < 6; ++site)
        {
            hits[site] = g_reachObserverCameraSiteHits[site].load(std::memory_order_relaxed);
            fixed[site] = g_reachObserverCameraCorrected[site].load(std::memory_order_relaxed);
            total += hits[site] + fixed[site];
            if (hits[site])
                mask |= (1u << site);
        }
        const uint32_t unknown = g_reachObserverCameraUnknownSite.load(std::memory_order_relaxed);
        if (unknown)
            mask |= (1u << 6);
        const uint64_t now = GetTickCount64();
        const bool setChanged = mask != lastMask;
        const bool periodic = total != lastTotal && lastReportMs != 0 && now - lastReportMs >= 30000;
        if (!setChanged && !periodic)
            return;
        lastMask = mask;
        lastReportMs = now;
        lastTotal = total;
        if (!mask)
            return;
        for (int site = 0; site < 6; ++site)
        {
            if (!hits[site])
                continue;
            const char* role = site == kReachObserverCameraWorldSite  ? "world render (never corrected)"
                               : site == kReachObserverCameraChudSite ? "CHUD marker projection"
                                                                      : "unidentified";
            LOG("Reach head-lock: site %d return +0x%llX %s - %u calls, "
                "%u head-locked",
                site, (unsigned long long)kReachObserverCameraReturnRvas[site], role, hits[site], fixed[site]);
        }
        if (unknown)
            LOG("Reach head-lock: %u calls from an unrecognised return address "
                "(left untouched)",
                unknown);
    }

    // Guarded read of Reach's native pause byte. Same shape as Halo 3's
    // ReadEnginePaused and ODST's ReadOdstEnginePaused: a value outside {0,1}
    // means the binding is no longer trustworthy, so report "unknown" and let
    // the caller behave exactly as it did before this feature existed.
    bool ReadReachEnginePaused(bool& paused)
    {
        const uintptr_t flag = g_reachNativePauseFlag.load(std::memory_order_acquire);
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
            GUARD_COUNT("reading Reach's pause flag faulted");
            return false;
        }
    }

    // 50 ms worker: locate Reach's native pause flag once per module
    // generation. Fail-open in every branch - a missing, ambiguous, or
    // out-of-range result logs once and leaves Reach behaving exactly as it did
    // before (always Gameplay while armed). It never blocks arming, never
    // disarms the camera core, and never touches Halo 3 or ODST.
    void ReachPauseColdPoll(uintptr_t base, size_t size, uint32_t generation, bool soleReachTitle)
    {
        static uint32_t attemptedGeneration = 0;
        if (!soleReachTitle || !base || !generation)
        {
            g_reachNativePauseFlag.store(0, std::memory_order_release);
            g_reachEnginePauseCache.store(-1, std::memory_order_release);
            attemptedGeneration = 0;
            return;
        }
        if (attemptedGeneration == generation)
            return;
        attemptedGeneration = generation;
        g_reachNativePauseFlag.store(0, std::memory_order_release);
        g_reachEnginePauseCache.store(-1, std::memory_order_release);

        const uintptr_t hit = sig::Find(base, size, kReachNativePauseOwnerSig);
        if (!hit || sig::Find(hit + 1, base + size - hit - 1, kReachNativePauseOwnerSig))
        {
            LOG("Reach pause state: owner signature %s; Reach keeps reporting "
                "gameplay while armed (no pause presentation)",
                hit ? "ambiguous" : "missing");
            return;
        }
        // The store is the last instruction of the signature:
        //   mov byte ptr [rip+disp32], r13b   (7 bytes, disp32 at +3)
        const uintptr_t store = hit + kReachNativePauseStoreOffset;
        const uintptr_t flag = sig::RipTarget(store + 3, hit + kReachNativePauseOwnerSigLength);
        if (flag < base || flag >= base + size)
        {
            LOG("Reach pause state: decoded flag falls outside haloreach.dll; "
                "pause presentation stays off");
            return;
        }
        uint8_t initial = 0xFF;
        __try
        {
            initial = *reinterpret_cast<const uint8_t*>(flag);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading Reach's initial pause flag faulted");
            initial = 0xFF;
        }
        if (initial > 1)
        {
            LOG("Reach pause state: flag failed boolean validation (%u); "
                "pause presentation stays off",
                static_cast<unsigned>(initial));
            return;
        }

        g_reachNativePauseFlag.store(flag, std::memory_order_release);
        LOG("Reach pause state: native flag at haloreach.dll+0x%llX "
            "(initial=%u, owner +0x%llX, expected +0x%llX/+0x%llX)",
            (unsigned long long)(flag - base), static_cast<unsigned>(initial), (unsigned long long)(hit - base),
            (unsigned long long)kReachNativePauseOwnerRva, (unsigned long long)kReachNativePauseFlagRva);
    }

    // 50 ms worker: locate the cinematic-globals registration once per module
    // generation and decode everything the sampler needs. Fail-open, one log
    // line either way; never touches the camera core.
    void ReachCineProbeColdPoll(uintptr_t base, size_t size, uint32_t generation, bool soleReachTitle)
    {
        if (!soleReachTitle || !base || !generation)
        {
            TitleAdapter_ClearCinematicControl(
                GameTitle::HaloReach, g_reachCamera.generation.load(std::memory_order_acquire));
            g_reachCineProbe.armed.store(false, std::memory_order_release);
            g_reachCineProbe.cutPrevValid = false;
            g_reachCineProbe.cutPrevPoseValid = false;
            return;
        }
        if (g_reachCineProbe.attemptedGeneration.load(std::memory_order_acquire) == generation)
            return;
        g_reachCineProbe.armed.store(false, std::memory_order_release);
        g_reachCineProbe.cutPrevValid = false;
        g_reachCineProbe.cutPrevPoseValid = false;
        g_reachCineProbe.attemptedGeneration.store(generation, std::memory_order_release);

        const uintptr_t hit = sig::Find(base, size, kReachCineRegistrationSig);
        if (!hit || sig::Find(hit + 1, base + size - hit - 1, kReachCineRegistrationSig))
        {
            LOG("REACHCINE: registration signature %s; probe off", hit ? "ambiguous" : "missing");
            return;
        }
        const uintptr_t tlsIndexAddr = sig::RipTarget(hit + 18, hit + 22);
        const uintptr_t nameAddr = sig::RipTarget(hit + 49, hit + 53);
        const uint32_t slotA = *reinterpret_cast<const uint32_t*>(hit + 70);
        const size_t verifyLength = strlen(kReachCineVerifyName) + 1;
        if (tlsIndexAddr < base || tlsIndexAddr + 4 > base + size || nameAddr < base ||
            nameAddr + verifyLength > base + size ||
            memcmp(reinterpret_cast<const void*>(nameAddr), kReachCineVerifyName, verifyLength) != 0)
        {
            LOG("REACHCINE: decoded operands failed verification; probe off");
            return;
        }
        if (slotA < 8 || slotA > 0x8000 || (slotA & 7) != 0)
        {
            LOG("REACHCINE: member A TLS slot 0x%X out of range; probe off", slotA);
            return;
        }
        uint32_t slotB = 0;
        const uintptr_t tailStart = hit + kReachCineSigLength;
        constexpr size_t kTailSpan = 0x80;
        if (tailStart + kTailSpan <= base + size)
        {
            const uintptr_t tail = sig::Find(tailStart, kTailSpan, kReachCineSlotBSig);
            if (tail)
            {
                const uint32_t candidate = *reinterpret_cast<const uint32_t*>(tail + 1);
                if (candidate >= 8 && candidate <= 0x8000 && (candidate & 7) == 0 && candidate != slotA)
                    slotB = candidate;
            }
        }

        g_reachCineProbe.tlsIndex = reinterpret_cast<uint32_t*>(tlsIndexAddr);
        g_reachCineProbe.slotA = slotA;
        g_reachCineProbe.slotB = slotB;
        g_reachCineProbe.sampleFailures.store(0, std::memory_order_relaxed);
        g_reachCineProbe.logReset.fetch_add(1, std::memory_order_release);
        g_reachCineProbe.armed.store(true, std::memory_order_release);
        LOG("REACHCINE: probe armed (tls index rva 0x%llX, member slots "
            "+0x%X/+0x%X%s); log-only",
            static_cast<unsigned long long>(tlsIndexAddr - base), slotA, slotB, slotB ? "" : " - member B unavailable");
    }

    // Worker-side diff of one probed member bank against the last values it
    // logged. Dwords that change on eight consecutive bursts are timer-like
    // and get muted so cut transitions stay readable.
    void ReachCineProbeDiffBank(char bank, const uint32_t* fresh, uint32_t* lastLogged, uint8_t* hotBursts,
        uint32_t& mutedMask, size_t count, unsigned& logged, unsigned& deferred)
    {
        for (size_t index = 0; index < count; ++index)
        {
            if (fresh[index] == lastLogged[index])
            {
                hotBursts[index] = 0;
                continue;
            }
            const uint32_t bit = 1u << index;
            if (mutedMask & bit)
            {
                lastLogged[index] = fresh[index];
                continue;
            }
            if (hotBursts[index] < 0xFF)
                ++hotBursts[index];
            if (hotBursts[index] >= 8)
            {
                mutedMask |= bit;
                lastLogged[index] = fresh[index];
                LOG("REACHCINE: %c+0x%02X changes every burst; muted "
                    "(timer-like)",
                    bank, static_cast<unsigned>(index * 4));
                continue;
            }
            if (logged >= 8)
            {
                ++deferred; // reported next burst
                continue;
            }
            LOG("REACHCINE: %c+0x%02X %08X -> %08X", bank, static_cast<unsigned>(index * 4), lastLogged[index],
                fresh[index]);
            lastLogged[index] = fresh[index];
            ++logged;
        }
    }

    // REACHHUD reporter. Runs on the 50 ms worker and states, in plain terms,
    // whether Reach is still drawing crosshair widgets, whether descriptors
    // went unreadable, and what the published art key is doing. One line per
    // state change; silent while healthy.
    void ReachChudDiagnosticTick(uint64_t now)
    {
        if (!g_reachCamera.armed.load(std::memory_order_acquire))
            return;
        static bool class2Present = true;
        static uint32_t reportedUnreadable = 0;
        static uint32_t reportedRejects = 0;
        static uint32_t reportedRedirect = 0;
        static uint64_t lastKeyLogMs = 0;
        static uint64_t loggedKey = 0;
        static uint64_t heightLastLogMs = 0;
        if (now - heightLastLogMs >= 2000 && g_reachHudHeightEnabled.load(std::memory_order_acquire))
        {
            heightLastLogMs = now;
            const auto applied = g_reachHudHeightApplied.exchange(0, std::memory_order_relaxed);
            const auto refused = g_reachHudHeightRefused.exchange(0, std::memory_order_relaxed);
            LOG("Reach HUD height: %.1f virtual px, %llu native anchors "
                "translated / %llu local stock refusals; camera core unaffected",
                g_config.hud_vertical_offset, static_cast<unsigned long long>(applied),
                static_cast<unsigned long long>(refused));
        }

        const uint64_t lastClass2 = g_reachChudLastClass2Ms.load(std::memory_order_relaxed);
        // Reach legitimately drops the crosshair briefly (reload, melee), and
        // the published art is deliberately held across those gaps. Two
        // seconds without a single class-2 draw is a different thing: the
        // engine stopped emitting the widget entirely, which is what the
        // player sees as "the crosshair disappeared". Wait for the first
        // sighting before claiming a drought - otherwise this reports one
        // 50 ms after arming, before Reach has drawn anything (observed).
        const bool present = lastClass2 == 0 || (now >= lastClass2 && now - lastClass2 < 2000);
        if (present != class2Present)
        {
            class2Present = present;
            if (present)
                LOG("REACHHUD: crosshair widgets are drawing again "
                    "(%u class-2 draws total)",
                    g_reachChudClass2Draws.load(std::memory_order_relaxed));
            else
            {
                // The decisive line: if otherDraws keeps climbing across the
                // drought, Reach IS still drawing widgets and our class
                // resolution is what changed; if it is flat too, the engine
                // really stopped. The histogram says which class the
                // crosshair's collection now reports.
                LOG("REACHHUD: no class-2 crosshair widget drawn for 2s "
                    "(unreadable %u, rejects %u, readable-non-class-2 %u)",
                    g_reachChudUnreadable.load(std::memory_order_relaxed),
                    g_reachChudRejects.load(std::memory_order_relaxed),
                    g_reachChudOtherDraws.load(std::memory_order_relaxed));
                LOG("REACHHUD: class histogram 0:%u 1:%u 2:%u 3:%u 4:%u 5:%u "
                    "6:%u 7:%u 8:%u other:%u",
                    g_reachChudClassCounts[0].load(std::memory_order_relaxed),
                    g_reachChudClassCounts[1].load(std::memory_order_relaxed),
                    g_reachChudClassCounts[2].load(std::memory_order_relaxed),
                    g_reachChudClassCounts[3].load(std::memory_order_relaxed),
                    g_reachChudClassCounts[4].load(std::memory_order_relaxed),
                    g_reachChudClassCounts[5].load(std::memory_order_relaxed),
                    g_reachChudClassCounts[6].load(std::memory_order_relaxed),
                    g_reachChudClassCounts[7].load(std::memory_order_relaxed),
                    g_reachChudClassCounts[8].load(std::memory_order_relaxed),
                    g_reachChudClassCounts[9].load(std::memory_order_relaxed));
            }
        }

        const uint32_t unreadable = g_reachChudUnreadable.load(std::memory_order_relaxed);
        if (unreadable != reportedUnreadable && now - lastKeyLogMs >= 2000)
        {
            reportedUnreadable = unreadable;
            lastKeyLogMs = now;
            LOG("REACHHUD: %u CHUD collection descriptors unreadable "
                "(last alternate-path flag %u) - those draws stayed stock",
                unreadable, g_reachChudLastUnreadablePath.load(std::memory_order_relaxed));
        }
        const uint32_t rejects = g_reachChudRejects.load(std::memory_order_relaxed);
        if (rejects != reportedRejects)
        {
            reportedRejects = rejects;
            LOG("REACHHUD: %u eye transactions rejected by the CHUD path", rejects);
        }
        const uint32_t redirect = g_reachChudRedirectUnavailable.load(std::memory_order_relaxed);
        if (redirect != reportedRedirect)
        {
            reportedRedirect = redirect;
            LOG("REACHHUD: crosshair redirect unavailable %u times - those "
                "eyes skipped publishing art; the camera core stayed armed",
                redirect);
        }
        const uint64_t key = g_reachAuthoredCrosshairKey.load(std::memory_order_relaxed);
        if (key != loggedKey)
        {
            loggedKey = key;
            LOG("REACHHUD: published crosshair art key -> %016llX", static_cast<unsigned long long>(key));
        }
    }

    // 50 ms worker: publish what the engine-thread sampler saw. All logging
    // lives here, off the render path, throttled to 250 ms bursts.
    void ReachCineProbeLogTick()
    {
        if (!g_reachCineProbe.armed.load(std::memory_order_acquire))
            return;
        static uint32_t seenReset = 0;
        static uint32_t lastSeqSeen = 0;
        static uint64_t lastSeqChangeMs = 0;
        static bool stallReported = false;
        static bool haveBaseline = false;
        static uint32_t lastLoggedA[kReachCineMemberADwords] = {};
        static uint32_t lastLoggedB[kReachCineMemberBDwords] = {};
        static uint8_t hotBurstsA[kReachCineMemberADwords] = {};
        static uint8_t hotBurstsB[kReachCineMemberBDwords] = {};
        static uint32_t mutedA = 0;
        static uint32_t mutedB = 0;
        static uint64_t lastBurstMs = 0;
        static uint32_t reportedFailures = 0;
        static uint64_t lastFailureReportMs = 0;

        const uint32_t reset = g_reachCineProbe.logReset.load(std::memory_order_acquire);
        if (reset != seenReset)
        {
            seenReset = reset;
            lastSeqSeen = 0;
            lastSeqChangeMs = 0;
            stallReported = false;
            haveBaseline = false;
            memset(hotBurstsA, 0, sizeof(hotBurstsA));
            memset(hotBurstsB, 0, sizeof(hotBurstsB));
            mutedA = 0;
            mutedB = 0;
            reportedFailures = 0;
        }

        const uint64_t now = GetTickCount64();
        const uint32_t failures = g_reachCineProbe.sampleFailures.load(std::memory_order_relaxed);
        if (failures != reportedFailures && now - lastFailureReportMs >= 5000)
        {
            reportedFailures = failures;
            lastFailureReportMs = now;
            LOG("REACHCINE: sampler failing (cumulative %u TLS/member read "
                "misses)",
                failures);
        }

        // Cutscene-facing confirmation, mirroring the Halo 3/ODST worker-side
        // report: one line per authored cut the render thread realigned to.
        static uint32_t lastCutCount = 0;
        const uint32_t cutCount = g_reachCineCutCount.load(std::memory_order_relaxed);
        if (cutCount != lastCutCount)
        {
            lastCutCount = cutCount;
            LOG("Reach cutscene facing: realigned to the authored camera "
                "(cut %u, %u of them from the camera-jump test)",
                cutCount, g_reachCineCutPoseCount.load(std::memory_order_relaxed));
        }
        ReachChudDiagnosticTick(now);

        uint32_t copyA[kReachCineMemberADwords];
        uint32_t copyB[kReachCineMemberBDwords];
        uint32_t stableSeq = 0;
        bool stable = false;
        for (int attempt = 0; attempt < 4 && !stable; ++attempt)
        {
            const uint32_t seqBefore = g_reachCineProbe.seq.load(std::memory_order_acquire);
            if (seqBefore & 1u)
                continue;
            memcpy(copyA, g_reachCineProbe.bufA, sizeof(copyA));
            memcpy(copyB, g_reachCineProbe.bufB, sizeof(copyB));
            std::atomic_thread_fence(std::memory_order_acquire);
            const uint32_t seqAfter = g_reachCineProbe.seq.load(std::memory_order_acquire);
            stable = seqBefore == seqAfter;
            stableSeq = seqBefore;
        }
        if (!stable)
            return;

        if (stableSeq != lastSeqSeen)
        {
            lastSeqSeen = stableSeq;
            lastSeqChangeMs = now;
            stallReported = false;
        }
        else
        {
            if (stableSeq != 0 && !stallReported && lastSeqChangeMs && now - lastSeqChangeMs > 3000)
            {
                stallReported = true;
                LOG("REACHCINE: sampler stalled >3s while armed - the owned "
                    "camera path is not running (cutscene or menu may bypass "
                    "the armed core)");
            }
            return;
        }

        if (!haveBaseline)
        {
            haveBaseline = true;
            memcpy(lastLoggedA, copyA, sizeof(lastLoggedA));
            memcpy(lastLoggedB, copyB, sizeof(lastLoggedB));
            LOG("REACHCINE: baseline A+00 %08X %08X %08X %08X %08X %08X %08X "
                "%08X",
                copyA[0], copyA[1], copyA[2], copyA[3], copyA[4], copyA[5], copyA[6], copyA[7]);
            LOG("REACHCINE: baseline A+20 %08X %08X %08X %08X %08X %08X %08X "
                "%08X",
                copyA[8], copyA[9], copyA[10], copyA[11], copyA[12], copyA[13], copyA[14], copyA[15]);
            LOG("REACHCINE: baseline B+00 %08X %08X %08X %08X", copyB[0], copyB[1], copyB[2], copyB[3]);
            return;
        }

        if (now - lastBurstMs < 250)
            return;
        unsigned logged = 0;
        unsigned deferred = 0;
        ReachCineProbeDiffBank('A', copyA, lastLoggedA, hotBurstsA, mutedA, kReachCineMemberADwords, logged, deferred);
        ReachCineProbeDiffBank('B', copyB, lastLoggedB, hotBurstsB, mutedB, kReachCineMemberBDwords, logged, deferred);
        if (deferred)
            LOG("REACHCINE: %u further changes deferred to next burst", deferred);
        if (logged || deferred)
            lastBurstMs = now;
    }
    void ReachCameraCore_Poll(uintptr_t base, size_t size, uint32_t generation, bool soleReachTitle, bool levelRunning)
    {
        // Each of these resolves by scanning haloreach.dll. They are module
        // touches with no use before the level runs, so they wait for the gate
        // exactly like the hooks do; teardown handling below still runs every
        // tick on soleReachTitle alone.
        const bool coldWorkAllowed = soleReachTitle && levelRunning;
        ReachActionTestGuard_Tick(base, size, generation, soleReachTitle, levelRunning);
        ReachCineProbeColdPoll(base, size, generation, coldWorkAllowed);
        ReachPauseColdPoll(base, size, generation, coldWorkAllowed);
        ReachMuzzleRetargetTick(base, size, generation, coldWorkAllowed);
        ReachFrameCapTick(base, size, generation, coldWorkAllowed, g_reachFrameCap, ResolveReachFrameCap,
            g_config.reach_uncap_framerate);
        ReachLightmapShadowLogTick();
        ReachWeatherLogTick();
        ReachSsaoLogTick();
        ReachObserverCameraLogTick();
        ReachMuzzleLogTick();
        ReachWorldWristWriteLogTick();
        ReachCineProbeLogTick();
        LogReachOuterCameraCommitIfReady();
        LogReachFpCameraUploadIfReady();
        LogReachFpStatusIfNew();
        ReportReachScope();
        ReachVehicleInputLogTick();
        const bool installed = g_reachCamera.installed.load(std::memory_order_acquire);
        if (installed && g_reachCamera.teardownRequested.load(std::memory_order_acquire))
        {
            RemoveReachCameraCore();
            return;
        }
        const TitleLevelGateAction gateAction = ResolveTitleLevelGateAction(soleReachTitle, installed, levelRunning);
        if (gateAction == TitleLevelGateAction::RemoveInstalledCore)
        {
            RemoveReachCameraCore();
            return;
        }
        if (gateAction == TitleLevelGateAction::RearmForFutureEntry)
        {
            // Title-exit re-arm, matching Halo 3, ODST, Halo 4 and Halo 2:
            // stillness seen while Reach is inactive must not satisfy the
            // frozen half of its next level's proof.
            g_reachLevelLoadGate.Rearm();
            return;
        }
        if (!levelRunning || !base || size != kReachRetailImageSize)
        {
            // Critical shared rule: while Reach is the active title but has no
            // installed core, HOLD the liveness evidence. Rearming here every
            // 50 ms prevented the gate from ever reaching its running state on
            // first entry, a cross-title switch, or a consecutive level.
            return;
        }
        if (installed && (base != g_reachCamera.base || generation != g_reachCamera.generation))
        {
            RemoveReachCameraCore();
            return;
        }
        if (g_vrRuntimeFailureLatched.load(std::memory_order_acquire))
        {
            g_reachCamera.armed.store(false, std::memory_order_release);
            return;
        }

        const ReachModuleEpoch epoch{base, generation};
        const ReachPreflightToken preflight = ReachRenderCandidate_GetPreflight(epoch);
        // Cold-prepare the authored-reticle swapchain/RTVs and the private
        // capture texture on this worker. Reach's hot CHUD capture entry
        // REFUSES to run without them (no lazy allocation in the hot hook),
        // and nothing else creates them for Reach: before the Reach camera
        // heartbeat existed, the shared snapshot spent most of its time unsettled, so
        // the capture entry constantly fell into the Halo 3/ODST lazy branch
        // by title misdetection and created the texture as a side effect.
        // Settling ownership removed that crutch, and Winter Contingency ran
        // its first 3.5 minutes with key-0 captures and no crosshair at all
        // (seen in a headset log). Idempotent and cheap
        // once created. A Failed result never blocks the camera core - the
        // feature degrades alone, loudly, per the failure-isolation rule.
        if (VR_CanPrepareAuthoredReticleResources())
        {
            const AuthoredReticlePreparationResult reticlePreparation = VR_PrepareAuthoredReticleResources();
            static uint32_t preparedLogGeneration = 0;
            static uint32_t failedLogGeneration = 0;
            if (reticlePreparation == AuthoredReticlePreparationResult::Ready && preparedLogGeneration != generation)
            {
                preparedLogGeneration = generation;
                LOG("Reach crosshair: authored capture resources "
                    "cold-prepared for generation %u",
                    generation);
            }
            else if (reticlePreparation == AuthoredReticlePreparationResult::Failed &&
                     failedLogGeneration != generation)
            {
                failedLogGeneration = generation;
                LOG("Reach crosshair: authored capture resource preparation "
                    "FAILED; authored captures stay off this generation");
            }
        }
        const bool ready = preflight.Complete() && ReachRenderCandidate_IsPreflightCurrent(preflight) &&
                           VR_ReachDisplayReady(epoch) && VR_CanPrepareAuthoredReticleResources();

        if (!installed)
        {
            // The gate was sampled once for the active title at the top of the
            // worker tick, BEFORE anything touched this module; levelRunning
            // carries that decision here. Sampling from title detection rather
            // than from proof-pass also fixed the slow entry into 3D: the
            // display proof's engine-fields often settle only once the level
            // is already rendering, so a gate that began observing there had
            // missed the loading screen's freeze and paid the full 6 s
            // already-running proof (one captured log: proof PASS,
            // gate open 6078 ms later, ~9 s of flat cinematic;
            // versus ~2 s on the same session's early-proof load).
            if (ready && levelRunning)
                InstallReachCameraCore(base, size, generation);
            return;
        }
        if (!g_reachCamera.armed.load(std::memory_order_acquire) && ready &&
            GetTickCount64() - g_reachCamera.installedAtMs >= kReachRenderSafetyIntervalMs)
        {
            g_reachCamera.armed.store(true, std::memory_order_release);
            LOG("Reach camera core armed: proven five-hook per-eye stereo + "
                "camera/FP transaction is live; failed owned eyes "
                "are revoked and never published");
            if (g_reachCamera.lightmapShadowsEnabled)
            {
                LOG("Reach world-shadow candidate ARMED with zero samples: "
                    "render_lightmap_shadows will be suppressed per owned eye");
            }
            if (g_reachCamera.ssaoIsolationActive.load(std::memory_order_acquire))
            {
                LOG("Reach SSAO candidate ARMED with zero suppressed-eye "
                    "samples; the first exact owned-eye execution will be "
                    "reported by the worker");
            }
        }
        // Parity with ODST: publish the lifecycle every tick so armed state and
        // capabilities stay current for shared features. Without this Reach's
        // arm-gated capabilities are masked off permanently.
        PublishReachLifecycle();
    }
} // namespace
namespace reach_bridge
{
    void TickReach(const TitleTickContext& c)
    {
        const TitleDescriptor* activeTitle = c.activeTitle;
        const bool activeLevelRunning = c.activeLevelRunning;
        const uint64_t pollNow = c.now;
        (void)pollNow;
        uint32_t& reachManualRecoveryPending = g_supervisorTitles.reachManualRecoveryPending;
        {
            const bool soleReachTitle = activeTitle && activeTitle->title == GameTitle::HaloReach &&
                                        TitleAdapter_GetActiveTitle() == GameTitle::HaloReach;
            uintptr_t reachBase = 0;
            size_t reachSize = 0;
            const uint32_t reachGeneration = soleReachTitle ? TitleAdapter_GetGeneration(GameTitle::HaloReach) : 0;
            const bool haveReachRange =
                soleReachTitle && sig::ModuleRange(activeTitle->moduleName, reachBase, reachSize);
            // The preflight scans the whole 74 MB image. It now keeps only
            // a non-owning exact-base identity; still withhold all module
            // touches until the gate proves the level is running.
            const bool reachLevelRunning = haveReachRange && activeLevelRunning;
            if (haveReachRange && reachManualRecoveryPending == reachGeneration &&
                !g_reachCamera.installed.load(std::memory_order_acquire))
            {
                g_reachChudParityFailedGeneration.store(0, std::memory_order_release);
                (void)ReachRenderCandidate_RetryFailed({reachBase, reachGeneration});
                reachManualRecoveryPending = 0;
            }
            ReachRenderCandidate_ColdPoll(reachBase, reachSize, reachGeneration, reachLevelRunning);
            VR_ReachRenderCandidate_ColdPoll();
            // Install/arm/remove the permanent Reach per-eye camera core.
            // It never installs until loaded-image preflight and VR
            // eye-capture proof pass, and never touches Halo 3/ODST.
            ReachCameraCore_Poll(reachBase, reachSize, reachGeneration, haveReachRange, reachLevelRunning);
            NativeReachSubtitles_Poll(reachBase, reachSize, reachGeneration, reachLevelRunning);
        }
    }
} // namespace reach_bridge
bool Game_OwnsReachAuthoredReticle()
{
    if (TitleAdapter_GetActiveTitle() != GameTitle::HaloReach)
        return false;
    const uint32_t cameraGeneration = g_reachCamera.generation.load(std::memory_order_acquire);
    return cameraGeneration != 0 && TitleAdapter_GetGeneration(GameTitle::HaloReach) == cameraGeneration &&
           g_reachCamera.installed.load(std::memory_order_acquire) &&
           g_reachCamera.armed.load(std::memory_order_acquire) &&
           !g_reachCamera.teardownRequested.load(std::memory_order_acquire) &&
           g_enabled.load(std::memory_order_acquire) && VR_IsStereoEnabled();
}
uint64_t Game_GetReachAuthoredCrosshairKey()
{
    return g_reachAuthoredCrosshairKey.load(std::memory_order_acquire);
}
void Game_RejectReachAuthoredReticle(uint32_t expectedGeneration, const char* reason)
{
    if (!expectedGeneration || TitleAdapter_GetActiveTitle() != GameTitle::HaloReach ||
        TitleAdapter_GetGeneration(GameTitle::HaloReach) != expectedGeneration ||
        !g_reachCamera.installed.load(std::memory_order_acquire))
    {
        return;
    }
    // This is the ONLY runtime path that disarms Reach and requests hook
    // removal. It used to do so silently, which made every Reach failure this
    // session unattributable: the log showed one good eye pass, then stereo
    // OFF, with nothing saying who decided that. Always name the caller.
    const bool alreadyRejected =
        g_reachChudParityFailedGeneration.exchange(expectedGeneration, std::memory_order_acq_rel) == expectedGeneration;
    if (!alreadyRejected)
    {
        LOG("Reach VR transaction rejected: %s; disarming and requesting hook "
            "teardown for generation %u",
            reason ? reason : "unspecified", static_cast<unsigned>(expectedGeneration));
    }
    g_reachCamera.armed.store(false, std::memory_order_release);
    g_reachCamera.teardownRequested.store(true, std::memory_order_release);
}
namespace reach_bridge
{
    // Reach presentation and HUD/pause context.
    // Returns true when this title owned the tick (Game_AutoVrTick returns immediately).
    bool AutoVrTickReach()
    {
        static bool wasReachHudContext = false;
        static bool wasReachNativePause = false;
        // Pause entry must hold 150 ms (a ~100 ms blip at load end flickered
        // 2D/3D); reset wherever wasReachNativePause is cleared.
        static pause_debounce::Entry reachPauseEntry;
        if (TitleAdapter_GetActiveTitle() == GameTitle::HaloReach)
        {
            wasReachHudContext = true;
            // Reach owns head tracking + stereo whenever its per-eye camera core is
            // armed. Enabling them lets the present path composite the two eye
            // caches the inner detour captured, and publishing Gameplay drives the
            // shared locomotion and haptics paths. Disarm mirrors ODST: drop
            // head tracking and detach so nothing composites stale eyes.
            if (g_reachCamera.armed.load(std::memory_order_acquire))
            {
                if (!g_enabled.load(std::memory_order_relaxed) || !VR_IsStereoEnabled())
                {
                    g_enabled.store(true, std::memory_order_release);
                    if (!VR_IsStereoEnabled())
                        VR_ToggleStereo();
                    LOG("Reach camera bring-up: head tracking, stereo, and 6DOF ON");
                }
                // Native pause, matching Halo 3's behavior rather than ODST's.
                // Halo 3 switches presentation to head-locked 2D and keeps its
                // camera core armed; ODST tears the whole core down for Save & Quit
                // safety, which is exactly what produced its slow-rearm defect. The
                // player asked for "flat head-locked view on pause, stereo back on
                // unpause", so Reach follows Halo 3. If the flag was never located
                // this reports unknown and Reach behaves exactly as it did before.
                bool reachPaused = false;
                const bool reachPauseKnown = ReadReachEnginePaused(reachPaused);
                g_reachEnginePauseCache.store(reachPauseKnown ? (reachPaused ? 1 : 0) : -1, std::memory_order_release);
                const bool reachPausedHeld = reachPauseEntry.Update(reachPauseKnown && reachPaused, GetTickCount64());
                if (reachPausedHeld && !wasReachNativePause)
                {
                    wasReachNativePause = true;
                    VR_RequestPausePresentation(true);
                    LOG("Reach pause presentation: native pause entered, "
                        "switching to head-locked 2D");
                }
                else if (!reachPausedHeld && wasReachNativePause)
                {
                    wasReachNativePause = false;
                    VR_RequestPausePresentation(false);
                    LOG("Reach pause presentation: native pause exited, "
                        "restoring stereo 3D");
                }
                const uint32_t reachGen = TitleAdapter_GetGeneration(GameTitle::HaloReach);
                if (reachGen)
                    TitleAdapter_PublishMode(
                        GameTitle::HaloReach, reachGen, reachPausedHeld ? RuntimeMode::Paused : RuntimeMode::Gameplay);
                // Same shared HUD behavior Halo 3 and ODST get, against Reach's own
                // record. An armed per-eye core is Reach's liveness proof; a failed
                // or ambiguous locate leaves Reach's HUD wholly stock and says so.
                if (!g_reachCamera.teardownRequested.load(std::memory_order_acquire))
                {
                    // Publish only the timestamp from the admitted Reach
                    // camera callback.  Present is shared by the MCC shell, so using
                    // GetTickCount64() here fabricated permanent Reach liveness after
                    // Save & Quit once the loader pin correctly pinned haloreach.dll.
                    const uint64_t reachLastCamMs = g_reachLastCamCopyMs.load(std::memory_order_acquire);
                    if (reachGen && reachLastCamMs)
                        TitleAdapter_PublishHeartbeat(GameTitle::HaloReach, reachGen, reachLastCamMs);

                    // A normal pause deliberately keeps the core armed for the
                    // head-locked 2D pause presentation.  Outside that pause, one
                    // full second without the real camera callback means the title
                    // no longer owns the frame (Save & Quit, shell, or an equivalent
                    // bypass). Request verified worker teardown while the
                    // loader pin still makes every MinHook target safely mapped.
                    if (reachLastCamMs && !(reachPauseKnown && reachPaused) && GetTickCount64() - reachLastCamMs > 1000)
                    {
                        g_reachCamera.teardownRequested.store(true, std::memory_order_release);
                    }
                    HudLayoutAutoTick(HudLayoutProfile::HaloReach);
                }
            }
            else
            {
                // Disarmed: a pause override must not outlive the camera core, or
                // the next arm comes back to a stranded 2D presentation.
                g_reachEnginePauseCache.store(-1, std::memory_order_release);
                reachPauseEntry.Reset();
                if (wasReachNativePause)
                {
                    wasReachNativePause = false;
                    VR_RequestPausePresentation(false);
                    LOG("Reach pause presentation: camera core disarmed, "
                        "clearing 2D pause override");
                }
                if (g_enabled.load(std::memory_order_relaxed) || VR_IsStereoEnabled())
                {
                    g_enabled.store(false, std::memory_order_release);
                    VR_DetachGamePresentation();
                }
            }
            return true;
        }
        if (wasReachHudContext)
        {
            wasReachHudContext = false;
            g_reachLastCamCopyMs.store(0, std::memory_order_release);
            g_reachEnginePauseCache.store(-1, std::memory_order_release);
            reachPauseEntry.Reset();
            // Leaving Reach through its own pause menu must not strand the next
            // title in the 2D override - the same rule Halo 3 applies at its
            // "title left, clearing 2D pause override" transition.
            if (wasReachNativePause)
            {
                wasReachNativePause = false;
                VR_RequestPausePresentation(false);
                LOG("Reach pause presentation: title left, "
                    "clearing 2D pause override");
            }
            InvalidateHudLayoutProfile(HudLayoutProfile::HaloReach);
        }
        return false;
    }
    // Reach reuses the shared closed-loop aim steering, but publishes no runtime
    // capability set (no Reach lifecycle), so TitleCapability_ControllerAim is
    // never enabled for it. Mirror the Reach-scoped predicate used by
    // Game_VrOwnsLookStick / Game_MoveStickIsLocomotion: while Reach's proven
    // stereo camera is armed and head tracking + VR aim are on, its aim may be
    // steered exactly like Halo 3's. Halo 3 / ODST are unaffected (title-gated).
    bool ReachControllerAimActive()
    {
        return TitleAdapter_GetActiveTitle() == GameTitle::HaloReach &&
               g_reachCamera.armed.load(std::memory_order_acquire) && g_enabled.load(std::memory_order_acquire) &&
               g_vrAim.load(std::memory_order_acquire) && VR_IsStereoEnabled();
    }
    int ReachVehicleTrimSlotNow()
    {
        if (TitleAdapter_GetActiveTitle() != GameTitle::HaloReach || !ReachVehicleFpActive())
        {
            return -1;
        }
        const uint32_t generation = TitleAdapter_GetGeneration(GameTitle::HaloReach);
        if (!generation || generation != g_reachCamera.generation.load(std::memory_order_acquire))
        {
            return -1;
        }
        return ReachVehicleTrimSnapshotSlot(
            g_reachVehicleTrimSnapshot.load(std::memory_order_acquire), generation, kReachVehicleSeatSlots);
    }
} // namespace reach_bridge
bool Game_ReachPlayerIsInVehicle()
{
    if (TitleAdapter_GetActiveTitle() != GameTitle::HaloReach ||
        !g_reachCamera.installed.load(std::memory_order_acquire) ||
        g_reachCamera.teardownRequested.load(std::memory_order_acquire) ||
        (g_reachCamera.vehicleInputBindingState.load(std::memory_order_acquire) !=
                static_cast<uint8_t>(ReachVehicleInputBindingState::Installed) &&
            g_reachCamera.vehicleCameraBindingState.load(std::memory_order_acquire) !=
                static_cast<uint8_t>(ReachVehicleCameraBindingState::Installed)))
        return false;
    const uint32_t generation = TitleAdapter_GetGeneration(GameTitle::HaloReach);
    if (g_reachCamera.vehicleCameraBindingState.load(std::memory_order_acquire) ==
        static_cast<uint8_t>(ReachVehicleCameraBindingState::Installed))
    {
        const uint64_t sampleMs = g_reachVehicleSampleMs.load(std::memory_order_relaxed);
        const uint64_t nowMs = GetTickCount64();
        if (!sampleMs || nowMs < sampleMs || nowMs - sampleMs > 500)
            return false;
    }
    return ReachVehicleInputSnapshotIsVehicle(g_reachVehicleInputSnapshot.load(std::memory_order_acquire), generation);
}
bool Game_TelemetryReadReachSeatedAim(
    float forwardOut[3], uint8_t& sourceOut, uint32_t& generationOut, uint64_t& sampleMsOut) noexcept
{
    sourceOut = 0;
    generationOut = 0;
    sampleMsOut = 0;
    if (!forwardOut)
        return false;
    // Admission mirrors ReachReadAimFeedback exactly (live camera generation,
    // 500 ms freshness, seated sources only, finite); the source ordinal,
    // generation and sampleMs additionally travel for telemetry provenance.
    const uint32_t generation = g_reachCamera.generation.load(std::memory_order_acquire);
    const uint64_t nowMs = GetTickCount64();
    auto& published = g_reachAimFeedback;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const uint32_t before = published.sequence.load(std::memory_order_acquire);
        if (!before || (before & 1u))
            continue;
        const uint32_t sampleGeneration = published.generation.load(std::memory_order_relaxed);
        const uint64_t sampleMs = published.sampleMs.load(std::memory_order_relaxed);
        const auto source = static_cast<ReachAimFeedbackSource>(published.source.load(std::memory_order_relaxed));
        const float sample[3] = {published.x.load(std::memory_order_relaxed),
            published.y.load(std::memory_order_relaxed), published.z.load(std::memory_order_relaxed)};
        if (published.sequence.load(std::memory_order_acquire) != before)
            continue;
        if (sampleGeneration != generation || !sampleMs || nowMs < sampleMs || nowMs - sampleMs > 500 ||
            source == ReachAimFeedbackSource::OnFootCompact || !std::isfinite(sample[0]) || !std::isfinite(sample[1]) ||
            !std::isfinite(sample[2]))
        {
            return false;
        }
        memcpy(forwardOut, sample, sizeof(sample));
        sourceOut = static_cast<uint8_t>(source);
        generationOut = sampleGeneration;
        sampleMsOut = sampleMs;
        return true;
    }
    return false;
}

bool Game_TelemetryReadReachCompletedEye(float eyeOut[3], uint64_t& preparedSerialOut) noexcept
{
    preparedSerialOut = 0;
    if (!eyeOut)
        return false;
    const uint32_t generation = g_reachCamera.generation.load(std::memory_order_acquire);
    if (!generation)
        return false;
    ReachCompletedEyeSnapshot sample{};
    if (!ReachReadCompletedEye(sample))
        return false;
    // Same-generation completed stereo-pair eye only; its preparedSerial
    // travels so analysis knows which frame rendered it. Finite-checked.
    if (sample.generation != generation || !sample.preparedSerial || !std::isfinite(sample.eye[0]) ||
        !std::isfinite(sample.eye[1]) || !std::isfinite(sample.eye[2]))
        return false;
    memcpy(eyeOut, sample.eye, sizeof(sample.eye));
    preparedSerialOut = sample.preparedSerial;
    return true;
}
namespace reach_bridge
{
    bool ReachPrimaryWeaponEvidence(uint32_t expectedGeneration, uint32_t& unitOut, uint32_t& weaponOut,
        uint32_t& detailOut, bool* primaryAbsentOut) noexcept
    {
        if (primaryAbsentOut)
            *primaryAbsentOut = false;
        if (!g_reachCamera.playerUnitByOutputUser || !g_reachCamera.unitInVehicle)
            return false;
        if (g_reachCamera.generation.load(std::memory_order_acquire) != expectedGeneration)
        {
            detailOut = kDiagnosticGuard;
            return false;
        }
        const uint32_t owner = uint32_t(g_reachCamera.playerUnitByOutputUser(0));
        if (owner == UINT32_MAX || !(owner >> 16))
            return false;
        if (g_reachCamera.unitInVehicle(int32_t(owner)))
            return false;
        if (!ReachMuzzleTargetStorage(owner))
            return false;
        uint32_t weapons[2]{};
        if (!ReachReadMuzzleWeapons(owner, weapons, primaryAbsentOut))
            return false;
        if (weapons[0] == UINT32_MAX)
            return false;
        detailOut = (weapons[1] != UINT32_MAX) ? 1u : 0u;
        if (g_reachCamera.base)
        {
            auto** slots = reinterpret_cast<uint8_t**>(__readgsqword(0x58));
            const uint32_t index = *reinterpret_cast<const uint32_t*>(g_reachCamera.base + kReachEngineTlsIndexRva);
            const auto* tls = slots && index < 0x200 ? slots[index] : nullptr;
            const auto* users = tls ? *reinterpret_cast<const uint8_t* const*>(tls + 0x6A0) : nullptr;
            if (users && *reinterpret_cast<const uint32_t*>(users + 0x3C) == weapons[0])
                detailOut |= kDiagnosticFpAgreement;
        }
        if (g_reachCamera.generation.load(std::memory_order_acquire) != expectedGeneration)
        {
            detailOut = kDiagnosticGuard;
            return false;
        }
        unitOut = owner;
        weaponOut = weapons[0];
        return true;
    }
    bool DiagnosticReachPrimary(
        uint32_t expectedGeneration, uint32_t& unitOut, uint32_t& weaponOut, uint32_t& detailOut) noexcept
    {
        return ReachPrimaryWeaponEvidence(expectedGeneration, unitOut, weaponOut, detailOut, nullptr);
    }
} // namespace reach_bridge

namespace reach_bridge
{
    // Core calls these Reach .inl functions. The .inl files stay inside the anonymous namespace above;
    // each wrapper only forwards, so core never sees the anonymous-namespace definitions.
    bool ReachContactMeleeReady()
    {
        return ::ReachContactMeleeReady();
    }
    void ReachPublishContactHand(int hand, const FpExplicitPoseTargets& targets, const FpInterpolationContext& context,
        const BoneMatrix& root, const BoneMatrix* solved, const float points[][3], int count, int32_t unit,
        uint32_t generation)
    {
        return ::ReachPublishContactHand(hand, targets, context, root, solved, points, count, unit, generation);
    }
    void ReportReachContactMelee()
    {
        return ::ReportReachContactMelee();
    }
    void ReportReachGrabPickup()
    {
        return ::ReportReachGrabPickup();
    }
    void ReportReachMuzzle()
    {
        return ::ReportReachMuzzle();
    }
    void ReportReachWeaponHaptics()
    {
        return ::ReportReachWeaponHaptics();
    }
    uint32_t ReachGrabChosenWeapon(uint64_t now)
    {
        return ::ReachGrabChosenWeapon(now);
    }
    bool ReachGrabPickupReady()
    {
        return ::ReachGrabPickupReady();
    }
    void ReachGrabSetSteer(bool steer)
    {
        return ::ReachGrabSetSteer(steer);
    }
    void ReachGrabPublishHead(const float q[4])
    {
        return ::ReachGrabPublishHead(q);
    }
    bool ReachHandOnFoot(uint64_t now)
    {
        return ::ReachHandOnFoot(now);
    }
    int ReachHandSeatKind(uint64_t now)
    {
        return static_cast<int>(::ReachHandSeat(now));
    }
    void ReachHandPostRequest(int kind, int slot)
    {
        return ::ReachHandPost(static_cast<holster::ProbeAction>(kind), slot);
    }
    bool ReachHandRead(uint32_t slots[4], int& role, int& planMode)
    {
        return ::ReachHandRead(slots, role, planMode);
    }
    bool ReachReadReserveWeapon(uint32_t owner, uint32_t& primary, uint32_t& reserve)
    {
        return ::ReachReadReserveWeapon(owner, primary, reserve);
    }
} // namespace reach_bridge
