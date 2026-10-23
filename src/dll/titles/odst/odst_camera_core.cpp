// odst_camera_core.cpp: ODST's camera core and everything built on it (moved from game.cpp, unchanged):
// the camera hooks and stereo eyes, HUD hooks, seats and vehicles, exposure lock and firing origin, the
// optional hooks, contact melee, muzzle, haptics, fingers, scope, install, lifecycle and retirement.
// Core reaches it through odst_bridge.h and it reaches core through halo3_family_internal.h (which
// includes game_internal.h); the ODST .inl files are included at the same points and scopes as before.
#include "../../core/halo3_family_internal.h"
#include "odst_bridge.h"
#include "../../core/guard_count.h"
using namespace game_core;

namespace
{
    // Inside the anonymous namespace, so the forwarders at the end can name the anonymous functions as ::Name.
    using namespace odst_bridge;
} // namespace

namespace
{
    constexpr uint32_t kOdstRuntimeCapabilities = kHalo3RuntimeCapabilities;
} // namespace
namespace odst_bridge
{
    // Defined with the ODST vehicle block far below; declared here because the
    // follow reader runs long before it.
    bool OdstFollowContextValid(uint32_t& outGeneration);
} // namespace odst_bridge
namespace
{
    struct CameraRuntimeLayout
    {
        size_t compactSize;
        size_t derivedSize;
        uintptr_t sourcePosition;
        uintptr_t sourceForward;
        uintptr_t sourceUp;
        uintptr_t sourceFpBlend;
        uintptr_t sourceVerticalOffset;
        uintptr_t sourceVerticalFov;
        uintptr_t sourceReferenceFov;
        uintptr_t compactPosition;
        uintptr_t compactForward;
        uintptr_t compactUp;
        uintptr_t compactModeFlags;
        uintptr_t compactFpBlend;
        uintptr_t rootCurrentCompact;
        uintptr_t rootCurrentDerived;
        uintptr_t rootSecondaryCompact;
        uintptr_t rootSecondaryDerived;
        uintptr_t nestedFpBase;
        uintptr_t nestedCurrentCompact;
        uintptr_t nestedCurrentDerived;
        uintptr_t nestedSecondaryCompact;
        uintptr_t nestedSecondaryDerived;
        uintptr_t nestedSourceCamera;
        uintptr_t windowBounds;
        uintptr_t renderBounds;
        uintptr_t activeBounds;
        uintptr_t verticalFov;
        uintptr_t referenceFov;
        uintptr_t verticalOffset;
        uintptr_t nearClip;
        uintptr_t farClip;
        uintptr_t obliquePlane;
        uintptr_t customProjection;
        uintptr_t customProjectionData;
        uintptr_t projectionMatrix;
        uintptr_t normalizedViewport;
        uintptr_t constructedViewSlot;
        uintptr_t viewCount;
        uintptr_t additionalContext;
        uintptr_t renderUserIndex;
        uintptr_t constructorResult;
        uintptr_t tableDerivedField;
        uintptr_t initializedZero;
        uintptr_t finalTailBoolean;
        size_t viewStride;
    };

    struct CameraRuntimeProfile
    {
        const wchar_t* moduleName;
        const char* displayName;
        uint32_t expectedTimestamp;
        size_t expectedImageSize;
        CameraRuntimeLayout layout;
        const char* camCopyPattern;
        const char* renderViewPattern;
        const char* prepareViewPattern;
        const char* buildViewportPattern;
        const char* buildMatricesPattern;
        const char* fpCameraRebuildPattern;
        const char* fpCameraUploadPattern;
        const char* fpDriverPattern;
        const char* fpDriverGuardPattern;
        const char* gunCameraConstructorPattern;
        const char* nativePauseOwnerPattern;
    };
} // namespace
namespace odst_bridge
{
    constinit OdstCameraRuntimeState g_odstCamera;
    constinit std::atomic<uint32_t> g_odstRuntimeGeneration{0};
} // namespace odst_bridge
namespace
{
    constinit thread_local bool g_odstPreparingEyeHud = false;
} // namespace
namespace odst_bridge
{
    constinit std::atomic<uintptr_t> g_odstNativePauseFlag{0};
    constinit std::atomic<float> g_odstRenderHalfFovX[2] = {{1.07338f}, {1.07338f}};
    constinit std::atomic<float> g_odstRenderHalfFovY[2] = {{0.92502f}, {0.92502f}};
} // namespace odst_bridge
namespace
{
    extern const CameraRuntimeProfile kOdstCameraProfile;
    constinit weapon_muzzle::Store g_odstMuzzles;
    constinit std::atomic<bool> g_odstMuzzleBindingsReady{false};
    bool OdstCaptureMuzzleOwner(int player, int slot, uint32_t& unit, uint32_t& weapon) noexcept;
    void OdstPublishMuzzlePalette(uint16_t tag, const FpInterpolationContext& context, const BoneMatrix* destination,
        uint32_t generation) noexcept;
    bool InstallOdstMuzzle(uintptr_t base, size_t size, uint32_t generation);
    bool RemoveOdstMuzzle();
    bool InstallOdstWeaponHaptics(uintptr_t base, size_t size, uint32_t generation);
    bool RemoveOdstWeaponHaptics();
    void ReportOdstWeaponHaptics();
    void OdstApplyControllerFingers(
        uint16_t tag, const FpInterpolationContext& context, BoneMatrix* destination, uint32_t generation) noexcept;
    void ReportOdstControllerFingers();
    void ReportOdstMuzzle();
} // namespace
namespace odst_bridge
{
    bool DiagnosticOdstPrimary(
        uint32_t expectedGeneration, uint32_t& unitOut, uint32_t& weaponOut, uint32_t& detailOut) noexcept;
} // namespace odst_bridge
namespace
{
    inline constexpr char kOdstCollisionVectorSignature[] =
        "48 8B C4 4C 89 40 18 88 50 10 48 89 48 08 55 53 56 57 41 54 "
        "41 55 41 56 41 57 48 8D A8 08 BB FF FF B8 B8 45 00 00 E8 ?? ?? ?? ?? 48 2B E0";
} // namespace
namespace odst_bridge
{
    constinit LegacyWorldCollisionFeature g_odstWorldCollision;
} // namespace odst_bridge
namespace
{
    bool OdstContactMeleeReady();
    bool InstallOdstContactMelee(uintptr_t base, size_t size, uint32_t generation);
    bool DisableAndRemoveOdstContactMelee();
    void ReportOdstContactMelee();
    void OdstPublishContactHand(int hand, const FpInterpolationContext& context, const BoneMatrix& root,
        const BoneMatrix* solved, const float points[][3], int count, int32_t unit, uint32_t generation);
    bool OdstRedirectContactVector(uintptr_t caller, uint64_t flags, int32_t mode, int32_t ignoredA, int32_t ignoredB,
        int32_t ignoredC, void* result, uint8_t& returned);
    __declspec(noinline) bool __fastcall OdstHudAnchorBasisHook(
        int userIndex, void* drawWidgetData, int anchorType, void* basis)
    {
        g_odstCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        const Halo3FamilyHudChainScope chainScope;
        HudAnchorBasisFn original = reinterpret_cast<HudAnchorBasisFn>(g_odstCamera.originalHudAnchorBasis);
        const bool result = original && original(userIndex, drawWidgetData, anchorType, basis);
        const uint32_t generation = g_odstRuntimeGeneration.load(std::memory_order_acquire);
        if (result && basis)
            Halo3FamilyObserveHudAnchor(anchorType, basis, g_odstHudSize, generation);
        if (result && basis && !g_authoredReticleCaptureStarted && g_odstCamera.armed.load(std::memory_order_acquire) &&
            !g_odstCamera.teardownRequested.load(std::memory_order_acquire) &&
            g_enabled.load(std::memory_order_relaxed) && VR_IsStereoEnabled())
        {
            const float height = g_config.hud_vertical_offset;
            if (isfinite(height) && height >= kHudHeightMin && height <= kHudHeightMax)
                reinterpret_cast<float*>(basis)[0x2C / sizeof(float)] -= height;
            if (chainScope.outermost)
                Halo3FamilyAdjustHudElement(basis, g_odstHudSize, generation);
        }
        g_odstCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        return result;
    }

    // ODST game brightness. Same three-float screen colour/gamma uploader as
    // Halo 3's, proven unique in halo3odst.dll by the shared `kHudXformSig`.
    // Its own trampoline, because MCC can
    // hold both title modules loaded at once and a shared original pointer
    // would let one title's teardown strand the other's call.
    constinit void* g_odstOriginalHudXform = nullptr;
    __declspec(noinline) void __fastcall OdstHudXformHook(float x, float y, float z)
    {
        g_odstCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        HudXformFn original = reinterpret_cast<HudXformFn>(g_odstOriginalHudXform);
        if (original)
        {
            const float b = GameBrightnessScale();
            if (b != 1.0f)
            {
                x *= b;
                y *= b;
            }
            original(x, y, z);
            static std::atomic<bool> logged{false};
            if (!logged.exchange(true))
                LOG("ODST brightness: hook active (game_brightness %.2f)", b);
        }
        g_odstCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }

    static bool OdstOwnsHudStereo()
    {
        return g_odstCamera.armed.load(std::memory_order_acquire) &&
               !g_odstCamera.teardownRequested.load(std::memory_order_acquire) &&
               g_enabled.load(std::memory_order_relaxed) && VR_IsStereoEnabled();
    }

    // ODST uses the shared Halo 3 crosshair behavior only while its own stereo
    // lifecycle is armed. The wrappers retain title-module/trampoline ownership
    // until callbacks return and fail open to stock rendering at every edge.
    __declspec(noinline) bool __fastcall OdstHudCrosshairVisibleHook(int userIndex)
    {
        g_odstCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        bool result = true;
        if (OdstOwnsHudStereo())
            result = HudCrosshairVisibleHook(userIndex);
        else if (g_gameIsPlayback && g_gameIsPlayback())
            result = g_realHudCrosshairVisible(userIndex);
        g_odstCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        return result;
    }

    __declspec(noinline) void __fastcall OdstHudDrawWidgetHook(
        int userIndex, void* descriptor, unsigned short widgetIndex, unsigned char useAlternatePath, void* drawState)
    {
        g_odstCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        if (g_scopeRenderActive.load(std::memory_order_acquire))
        {
            ++hud_visibility::depth;
            __try
            {
                g_realHudDrawWidget(userIndex, descriptor, widgetIndex, useAlternatePath, drawState);
            }
            __finally
            {
                --hud_visibility::depth;
                g_odstCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
            }
            return;
        }
        if (OdstOwnsHudStereo())
            HudDrawWidgetHook(userIndex, descriptor, widgetIndex, useAlternatePath, drawState);
        else
            g_realHudDrawWidget(userIndex, descriptor, widgetIndex, useAlternatePath, drawState);
        g_odstCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
} // namespace
namespace odst_bridge
{
    constinit std::atomic<uint64_t> g_odstLastCamCopyMs{0};
} // namespace odst_bridge
namespace
{
    // The window table lives in title_runtime_state.h so the offline tests
    // pin every value; the Reach zero-window incident is documented there.
    static_assert(kOdstTitleRuntimeHeartbeatWindowMs == kOdstCameraHardTimeoutMs + 1,
        "the ODST heartbeat window must outlast the ODST camera hard timeout");
} // namespace
namespace odst_bridge
{
    bool PublishOdstLifecycle()
    {
        const uint32_t generation = g_odstRuntimeGeneration.load(std::memory_order_acquire);
        if (!generation)
            return false;
        TitleRuntimeLifecycle lifecycle{};
        lifecycle.installed = g_odstCamera.installed.load(std::memory_order_acquire);
        lifecycle.armed = g_odstCamera.armed.load(std::memory_order_acquire);
        lifecycle.teardownRequested = g_odstCamera.teardownRequested.load(std::memory_order_acquire);
        lifecycle.enabledCapabilities =
            lifecycle.installed && !lifecycle.teardownRequested ? kOdstRuntimeCapabilities : TitleCapability_None;
        if (!g_cinematicTlsIndex || !g_odstCamera.cinematicCameraTlsMemberOffset)
            lifecycle.enabledCapabilities &= ~TitleCapability_CutsceneTheater;
        return TitleAdapter_PublishLifecycle(GameTitle::Halo3ODST, generation, lifecycle);
    }
} // namespace odst_bridge
namespace
{
    void ClearOdstRuntimePublication()
    {
        const uint32_t generation = g_odstRuntimeGeneration.load(std::memory_order_acquire);
        if (!generation)
            return;
        TitleAdapter_PublishLifecycle(GameTitle::Halo3ODST, generation, {});
        TitleAdapter_ClearHeartbeat(GameTitle::Halo3ODST, generation);
        TitleAdapter_ClearCinematicControl(GameTitle::Halo3ODST, generation);
        g_odstLastCamCopyMs.store(0, std::memory_order_release);
        g_odstRuntimeGeneration.store(0, std::memory_order_release);
    }
} // namespace
namespace odst_bridge
{
    constinit HudLayoutRememberedCache g_odstHudRemembered{};
    // ODST's equivalent, so the shared hand solver can hang the arms off an
    // ODST seat too. Declared here because the solver runs long before the ODST
    // vehicle block is defined.
    bool OdstComputeSeatBodyAnchor(float out[3]);
} // namespace odst_bridge
namespace
{
    __declspec(noinline) uint8_t __fastcall OdstCollisionResolveDetour(uint64_t flags, int32_t mode, const float* start,
        const float* desired, int32_t ignoredObjectA, int32_t ignoredObjectB, int32_t ignoredObjectC, void* result)
    {
        g_odstWorldCollision.callbacks.fetch_add(1, std::memory_order_acq_rel);
        uint8_t nativeResult = 0;
        __try
        {
            if (!OdstRedirectContactVector(reinterpret_cast<uintptr_t>(_ReturnAddress()), flags, mode, ignoredObjectA,
                    ignoredObjectB, ignoredObjectC, result, nativeResult))
                nativeResult = LegacyCollisionSchedulerDetourBody(g_odstWorldCollision, flags, mode, start, desired,
                    ignoredObjectA, ignoredObjectB, ignoredObjectC, result);
        }
        __finally
        {
            g_odstWorldCollision.callbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
        return nativeResult;
    }
} // namespace
namespace odst_bridge
{
    CinematicControlState ReadOdstCinematicControl(int32_t& scene, int32_t& shot)
    {
        const CinematicControlState cinematicState = ReadCinematicControl(scene, shot);
        if (cinematicState != CinematicControlState::AuthoredLocked)
            return cinematicState;

        bool constraintsAvailable = false;
        float maximumLookAngles[4]{};
        float maximumLookAngleRates[4]{};
        int32_t interpolationTicksRemaining = 0;
        const uint32_t cameraMemberOffset = g_odstCamera.cinematicCameraTlsMemberOffset;
        __try
        {
            auto** slots = reinterpret_cast<void**>(__readgsqword(0x58));
            if (slots && g_cinematicTlsIndex && *g_cinematicTlsIndex < 256 && cameraMemberOffset)
            {
                auto* tls = reinterpret_cast<unsigned char*>(slots[*g_cinematicTlsIndex]);
                auto* camera = tls ? *reinterpret_cast<unsigned char**>(tls + cameraMemberOffset) : nullptr;
                if (camera && *reinterpret_cast<const uint16_t*>(camera + 2) == 5)
                {
                    constexpr uint32_t kCurrentOffsets[4] = {0xC8, 0xCC, 0xD0, 0xD4};
                    constexpr uint32_t kRateOffsets[4] = {0xD8, 0xDC, 0xE0, 0xE4};
                    for (int axis = 0; axis < 4; ++axis)
                    {
                        maximumLookAngles[axis] = *reinterpret_cast<const float*>(camera + kCurrentOffsets[axis]);
                        maximumLookAngleRates[axis] = *reinterpret_cast<const float*>(camera + kRateOffsets[axis]);
                    }
                    interpolationTicksRemaining = *reinterpret_cast<const int32_t*>(camera + 0xF0);
                    constraintsAvailable = true;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading ODST's cinematic control faulted");
            constraintsAvailable = false;
        }
        return ClassifyOdstCinematicControl(cinematicState, constraintsAvailable, maximumLookAngles,
            maximumLookAngleRates, interpolationTicksRemaining);
    }
} // namespace odst_bridge
namespace
{
    void OdstPreparePhysicalCrouchCamera(uintptr_t base, size_t size);
    void OdstApplyPhysicalCrouchCamera(float* position, float physicalDown, bool playerControlled);
    // ODST's own vehicle binding. Deliberately a separate set of globals
    // from the Halo 3 ones above - the two titles' offsets differ, and only
    // one title's camera core is ever installed, so sharing state could only
    // ever let a stale pointer outlive its module.
    //
    // ODST's engine TLS index is decoded from its OWN matched native body.
    // g_engineTlsIndex is written only by the Halo 3 install and is never
    // cleared, so it must never be reused here.
    constinit uint32_t* g_odstEngineTlsIndex = nullptr;
} // namespace
namespace odst_bridge
{
    constinit std::atomic<uint8_t> g_odstVehicleBinding{static_cast<uint8_t>(OdstVehicleBindingState::NotInstalled)};
    constinit Halo3PlayerUnitGetterFn g_odstPlayerUnitGetter = nullptr;
} // namespace odst_bridge
namespace
{
    constinit Halo3UnitInVehicleFn g_odstUnitInVehicle = nullptr;
    constinit Halo3VehicleTypeFn g_odstVehicleTypeAccessor = nullptr;
    constinit Halo3InterpolatedNodesFn g_odstInterpolatedNodes = nullptr;
    constinit Halo3MarkersInternalFn g_odstMarkersInternal = nullptr;
    constinit void** g_odstTagInstanceTable = nullptr;
} // namespace
namespace odst_bridge
{
    constinit void** g_odstTagDataBase = nullptr;
} // namespace odst_bridge
namespace
{
    constinit std::atomic<uint8_t> g_odstNodeBinding{static_cast<uint8_t>(OdstVehicleBindingState::NotInstalled)};
    constinit std::atomic<uint64_t> g_odstVehicleProbeMs{0};
    constinit std::atomic<uint32_t> g_odstVehicleProbeFaults{0};
    // ODST's own render_exposure_lock byte, in both of the copies the engine
    // keeps: the EDITABLE block it rebuilds from every frame, and the INTERNAL
    // block the eye-adaptation integrator actually reads. Engaged only while a
    // first-person seat owns the view, so ordinary play keeps its adaptation.
    constinit uint8_t* g_odstExposureLock = nullptr;
    constinit uint8_t* g_odstExposureEditableLock = nullptr;
    constinit std::atomic<uint32_t> g_odstExposureLockState{0};
    constinit std::atomic<uint32_t> g_odstExposureEngages{0};
    // Reads back the byte the ENGINE holds a frame after we engage, so the log
    // reports whether the hold actually took rather than only that we wrote it.
    // That distinction is exactly what let an inert lock ship.
    constinit std::atomic<uint32_t> g_odstExposureVerifyPending{0};
    constinit std::atomic<uint32_t> g_odstExposureVerified{0};

    // The personal-weapon shot line. The engine's weapon-barrel code calls the
    // occupant's camera-position evaluator into its OWN stack buffer and then
    // projects the muzzle onto that ray, so a seated occupant's shot line is
    // exactly (camera position, aiming vector). In a first-person seat we
    // render the eye at the authored seat anchor plus the player's trims, which
    // is NOT that point, so the sight line and the shot line are parallel --
    // and two parallel lines meet at no distance at all. Re-aiming can only
    // make them cross at one chosen range; moving the shot origin onto the
    // rendered eye makes them the same line everywhere.
    //
    // This is deliberately scoped to that ONE call site. The same evaluator
    // also places the engine's first-person camera (we clear the seat's
    // third-person bit, which is precisely what routes the engine through it),
    // so overriding it wholesale would feed our own output back in as the next
    // frame's camera origin and would also move the hands, the cutscene camera
    // and the chase camera. Gating on the return address keeps all of that
    // stock and moves only the bullet.
    using OdstUnitCameraPositionFn = void(__fastcall*)(int32_t, float*, uint8_t);
    constinit OdstUnitCameraPositionFn g_origOdstUnitCameraPosition = nullptr;
    constinit void* g_odstUnitCameraPositionTarget = nullptr;
    constinit uintptr_t g_odstFiringOriginReturn = 0;
    constinit std::atomic<int32_t> g_odstSeatedPlayerUnit{-1};
    constinit std::atomic<uint32_t> g_odstCinematicLocked{0};
    constinit std::atomic<uint32_t> g_odstFiringOriginMoves{0};
    constinit std::atomic<uint32_t> g_odstFiringOriginSkips{0};
} // namespace
namespace odst_bridge
{
    // Is the wheel available in this seat? Aircraft are look-steered but take
    // the plain flight stick, not a wheel.
    bool OdstSeatUsesWheel();
    bool OdstSeatAuthorsSteeringNow();
} // namespace odst_bridge
namespace
{
    // A camera gets first-person aim/control ownership when its blend is at
    // least this close to 1. On-foot and vehicle views use that path and the
    // internal scene-color target. The blend-0 death camera remains a distinct
    // third-person mode: it gets headset camera ownership but no weapon aim,
    // and its completed per-eye draws are captured from the direct backbuffer.
    void OdstRequestPresentationDetach()
    {
        g_odstCamera.presentationDetachRequested.fetch_add(1, std::memory_order_acq_rel);
    }

    bool OdstPresentationDetachOwned()
    {
        const uint64_t completed = g_odstCamera.presentationDetachCompleted.load(std::memory_order_acquire);
        const uint64_t requested = g_odstCamera.presentationDetachRequested.load(std::memory_order_acquire);
        return requested != completed;
    }
} // namespace
namespace odst_bridge
{
    bool OdstCameraOnlyContext()
    {
        const bool runtimeStateOwned =
            g_odstCamera.installed.load(std::memory_order_acquire) || OdstPresentationDetachOwned();
        const TitleDescriptor* title = TitleAdapter_GetActive();
        const bool adapterReportsOdst = title && title->title == GameTitle::Halo3ODST;
        const bool privateBuildEnabled =
            TitleRegistry_HookPlan(GameTitle::Halo3ODST) == TitleHookPlan::OdstExperimentalCameraCore;
        return OdstCameraOnlyScopeRequired(privateBuildEnabled, adapterReportsOdst, runtimeStateOwned);
    }
} // namespace odst_bridge
namespace
{
    bool ReadOdstEnginePaused(bool& paused)
    {
        const uintptr_t address = g_odstNativePauseFlag.load(std::memory_order_acquire);
        if (!address)
            return false;
        __try
        {
            const uint8_t value = *reinterpret_cast<const uint8_t*>(address);
            if (value > 1)
                return false;
            paused = value != 0;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading ODST's pause flag faulted");
            return false;
        }
    }
} // namespace
namespace odst_bridge
{
    void OdstRequestFallback(OdstFallbackReason reason)
    {
        int expected = static_cast<int>(OdstFallbackReason::None);
        g_odstCamera.fallbackReason.compare_exchange_strong(
            expected, static_cast<int>(reason), std::memory_order_acq_rel);
        // Let an in-flight outer renderer finish both eyes coherently. Teardown
        // disables its entry before disarming the FP dependencies.
        g_odstCamera.cameraArrayReady.store(false, std::memory_order_release);
        g_odstCamera.teardownRequested.store(true, std::memory_order_release);
        PublishOdstLifecycle();
        OdstRequestPresentationDetach();
    }
} // namespace odst_bridge
namespace
{
    bool OdstBasisIsActive(const char* bytes, uintptr_t positionOffset, uintptr_t forwardOffset, uintptr_t upOffset)
    {
        if (!bytes)
            return false;
        const float* position = reinterpret_cast<const float*>(bytes + positionOffset);
        const float* forward = reinterpret_cast<const float*>(bytes + forwardOffset);
        const float* up = reinterpret_cast<const float*>(bytes + upOffset);
        for (int axis = 0; axis < 3; ++axis)
            if (!isfinite(position[axis]) || !isfinite(forward[axis]) || !isfinite(up[axis]))
                return false;
        const float forwardLength = forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2];
        const float upLength = up[0] * up[0] + up[1] * up[1] + up[2] * up[2];
        const float crossX = forward[1] * up[2] - forward[2] * up[1];
        const float crossY = forward[2] * up[0] - forward[0] * up[2];
        const float crossZ = forward[0] * up[1] - forward[1] * up[0];
        const float crossLength = crossX * crossX + crossY * crossY + crossZ * crossZ;
        return forwardLength > 0.25f && forwardLength < 4.0f && upLength > 0.25f && upLength < 4.0f &&
               crossLength > 0.0625f;
    }

    bool OdstSourceCameraIsActive(const void* source)
    {
        const auto& layout = kOdstCameraProfile.layout;
        return OdstBasisIsActive(
            static_cast<const char*>(source), layout.sourcePosition, layout.sourceForward, layout.sourceUp);
    }

    bool OdstCompactCameraIsActive(const void* compact)
    {
        const auto& layout = kOdstCameraProfile.layout;
        return OdstBasisIsActive(
            static_cast<const char*>(compact), layout.compactPosition, layout.compactForward, layout.compactUp);
    }

    // Any active plain-perspective slot-0 camera has a proven layout for the
    // per-eye camera rewrite. First-person and vehicle renders use the internal
    // scene-color target; the blend-0 death camera uses a direct backbuffer
    // capture after the same camera rewrite.
    bool OdstCompactCameraIsStereoRedirectable(const void* compact)
    {
        if (!OdstCompactCameraIsActive(compact))
            return false;
        const auto& layout = kOdstCameraProfile.layout;
        const char* bytes = static_cast<const char*>(compact);
        const uint32_t modeFlags = *reinterpret_cast<const uint32_t*>(bytes + layout.compactModeFlags);
        const float verticalFov = *reinterpret_cast<const float*>(bytes + layout.verticalFov);
        const float referenceFov = *reinterpret_cast<const float*>(bytes + layout.referenceFov);
        const float verticalOffset = *reinterpret_cast<const float*>(bytes + layout.verticalOffset);
        const float nearClip = *reinterpret_cast<const float*>(bytes + layout.nearClip);
        const float farClip = *reinterpret_cast<const float*>(bytes + layout.farClip);
        const float* oblique = reinterpret_cast<const float*>(bytes + layout.obliquePlane);
        const uint32_t customProjection = *reinterpret_cast<const uint32_t*>(bytes + layout.customProjection);
        const float* customData = reinterpret_cast<const float*>(bytes + layout.customProjectionData);
        const auto boundsAreOrdered = [bytes](uintptr_t offset)
        {
            const int16_t* bounds = reinterpret_cast<const int16_t*>(bytes + offset);
            return bounds[0] < bounds[2] && bounds[1] < bounds[3];
        };
        return OdstPlainPerspectiveScalars(modeFlags, verticalFov, referenceFov, verticalOffset, nearClip, farClip) &&
               boundsAreOrdered(layout.windowBounds) && boundsAreOrdered(layout.renderBounds) &&
               boundsAreOrdered(layout.activeBounds) && oblique[0] == 0.0f && oblique[1] == 0.0f &&
               oblique[2] == 0.0f && oblique[3] == 0.0f && customProjection == 0 && customData[0] == 0.0f &&
               customData[1] == 0.0f && customData[2] == 0.0f && customData[3] == 0.0f;
    }

    bool OdstCompactCameraUsesProvenMode(const void* compact)
    {
        if (!OdstCompactCameraIsStereoRedirectable(compact))
            return false;
        const auto& layout = kOdstCameraProfile.layout;
        const float fpBlend =
            *reinterpret_cast<const float*>(static_cast<const char*>(compact) + layout.compactFpBlend);
        return OdstFirstPersonControlBlend(fpBlend);
    }

    bool OdstSingleUserTailIsValid(const void* view)
    {
        if (!view)
            return false;
        const auto& layout = kOdstCameraProfile.layout;
        const char* bytes = static_cast<const char*>(view);
        return *reinterpret_cast<const int32_t*>(bytes + layout.constructedViewSlot) == 0 &&
               *reinterpret_cast<const int32_t*>(bytes + layout.viewCount) == 1 &&
               *reinterpret_cast<const int32_t*>(bytes + layout.additionalContext) == 0 &&
               *reinterpret_cast<const int32_t*>(bytes + layout.renderUserIndex) == 0 &&
               *reinterpret_cast<const int32_t*>(bytes + layout.constructorResult) == 0 &&
               *reinterpret_cast<const int32_t*>(bytes + layout.tableDerivedField) == 0 &&
               *reinterpret_cast<const int32_t*>(bytes + layout.initializedZero) == 0 &&
               *reinterpret_cast<const uint8_t*>(bytes + layout.finalTailBoolean) == 0;
    }

    bool OdstCameraArraySupportsMode(uintptr_t cameraArray, bool requireFirstPerson)
    {
        if (!cameraArray)
            return false;
        const auto& layout = kOdstCameraProfile.layout;
        const char* view = reinterpret_cast<const char*>(cameraArray);
        const void* compact = view + layout.rootCurrentCompact;
        const bool modeValid = requireFirstPerson ? OdstCompactCameraUsesProvenMode(compact)
                                                  : OdstCompactCameraIsStereoRedirectable(compact);
        if (!OdstSingleUserTailIsValid(view) || !modeValid)
            return false;

        // The nested FP driver publishes its source pointer after the root
        // camera becomes usable. A null pointer is therefore a valid pre-hook
        // installation state; any non-null pointer must still own slot 0's
        // compact camera exactly.
        const uintptr_t nestedSource = *reinterpret_cast<const uintptr_t*>(view + layout.nestedSourceCamera);
        if (!OdstNestedSourceIsCompatible(nestedSource, cameraArray + layout.rootCurrentCompact))
            return false;

        // Constructors may leave non-camera bookkeeping in the unused objects.
        // The single-user tail is authoritative; reject only another ACTIVE
        // compact camera rather than demanding byte-for-byte zero storage.
        bool inactive[3]{};
        for (size_t slot = 1; slot < 4; ++slot)
        {
            const char* candidate = view + slot * layout.viewStride;
            inactive[slot - 1] = OdstCompactCameraIsActive(candidate + layout.rootCurrentCompact);
        }
        return OdstInactiveCameraSlotsAreSafe(inactive[0], inactive[1], inactive[2]);
    }

    bool OdstCameraArraySupportsBringup(uintptr_t cameraArray)
    {
        // Parity fix (cutscene 3D): VR arms on ANY active, plain-perspective,
        // slot-0 single-user camera -- not only a first-person one. Levels that
        // OPEN with a cinematic (ODST's drop-pod intro) present a blend-0
        // cutscene camera before any first-person gameplay camera exists.
        // Gating arm on first person left those whole cutscenes flat 2D with
        // stereo off (confirmed in the headset log: never armed during the
        // intro). The stereo redirect (Build G) and OdstApplyHeadLook already
        // drive any active redirectable camera once armed -- that is why the
        // blend-0 vehicle works -- so arming on the same redirect predicate
        // makes the opening cutscene render stereo + head-tracked like Halo 3.
        // First-person blend still gates CONTROLS (aim/head-look ownership)
        // separately in OdstCamCopyBody; it never gated whether stereo is on.
        return OdstCameraArraySupportsMode(cameraArray, false);
    }

    bool OdstCameraArraySupportsStereoRedirect(uintptr_t cameraArray)
    {
        return OdstCameraArraySupportsMode(cameraArray, false);
    }

    void LogOdstCameraReadiness(uintptr_t cameraArray)
    {
        if (!cameraArray)
            return;
        const auto& layout = kOdstCameraProfile.layout;
        const char* view = reinterpret_cast<const char*>(cameraArray);
        const char* compact = view + layout.rootCurrentCompact;
        const int32_t* tail = reinterpret_cast<const int32_t*>(view + layout.constructedViewSlot);
        const uint32_t mode = *reinterpret_cast<const uint32_t*>(compact + layout.compactModeFlags);
        const float blend = *reinterpret_cast<const float*>(compact + layout.compactFpBlend);
        const float fov = *reinterpret_cast<const float*>(compact + layout.verticalFov);
        const float reference = *reinterpret_cast<const float*>(compact + layout.referenceFov);
        const float offset = *reinterpret_cast<const float*>(compact + layout.verticalOffset);
        const float nearClip = *reinterpret_cast<const float*>(compact + layout.nearClip);
        const float farClip = *reinterpret_cast<const float*>(compact + layout.farClip);
        const uintptr_t nestedSource = *reinterpret_cast<const uintptr_t*>(view + layout.nestedSourceCamera);
        bool inactive[3]{};
        for (size_t slot = 1; slot < 4; ++slot)
            inactive[slot - 1] = OdstCompactCameraIsActive(view + slot * layout.viewStride + layout.rootCurrentCompact);
        LOG("ODST camera readiness: tail=[%d,%d,%d,%d,%d,%d,%d,%u] "
            "mode=0x%08X blend=%.6f fov=%.6f ref=%.6f offset=%.6f "
            "near=%.6f far=%.3f",
            tail[0], tail[1], tail[2], tail[3], tail[4], tail[5], tail[6],
            static_cast<unsigned>(*reinterpret_cast<const uint8_t*>(view + layout.finalTailBoolean)), mode, blend, fov,
            reference, offset, nearClip, farClip);
        LOG("ODST camera readiness: nestedSource=%p expected=%p "
            "inactiveSlots=%d/%d/%d",
            reinterpret_cast<void*>(nestedSource), reinterpret_cast<void*>(cameraArray + layout.rootCurrentCompact),
            inactive[0] ? 1 : 0, inactive[1] ? 1 : 0, inactive[2] ? 1 : 0);
    }

    // Diagnostic (worker-only): while the core is installed but NOT yet armed,
    // log the live camera-array readiness whenever it materially changes
    // (rate-limited). If a cutscene keeps the view flat, this shows the actual
    // cutscene-camera state -- active, and whether it satisfies the stereo
    // redirect gate that now also drives arming -- so we can tell an arm that
    // reached the camera from a genuinely different (custom-projection) camera.
    void LogOdstWaitingReadinessIfChanged(uintptr_t cameraArray)
    {
        static uint32_t lastSig = 0xFFFFFFFFu;
        static uint64_t lastLogMs = 0;
        if (!cameraArray)
            return;
        const auto& layout = kOdstCameraProfile.layout;
        const char* compact = reinterpret_cast<const char*>(cameraArray) + layout.rootCurrentCompact;
        const uint32_t mode = *reinterpret_cast<const uint32_t*>(compact + layout.compactModeFlags);
        const bool active = OdstCompactCameraIsActive(compact);
        const bool redirectable = OdstCompactCameraIsStereoRedirectable(compact);
        const bool tailValid = OdstSingleUserTailIsValid(reinterpret_cast<const void*>(cameraArray));
        const uint32_t sig = (mode & 0x3FFFu) | (static_cast<uint32_t>(active) << 16) |
                             (static_cast<uint32_t>(redirectable) << 17) | (static_cast<uint32_t>(tailValid) << 18);
        const uint64_t now = GetTickCount64();
        if (sig == lastSig && now - lastLogMs < 2000)
            return;
        lastSig = sig;
        lastLogMs = now;
        LOG("ODST camera WAIT: active=%d tailValid=%d redirectable=%d "
            "(arm-eligible=%s) -- core installed, not yet armed",
            active ? 1 : 0, tailValid ? 1 : 0, redirectable ? 1 : 0,
            (active && tailValid && redirectable) ? "YES" : "NO");
        LogOdstCameraReadiness(cameraArray);
    }

    void ApplyOdstMotionBlurSetting()
    {
        if (!g_odstCamera.motionBlurResolved)
            return;
        if (!g_config.motion_blur)
        {
            for (OdstMotionBlurVar& var : g_odstCamera.motionBlurVars)
            {
                if (*var.slot != 0.0f)
                    var.original = *var.slot;
                *var.slot = 0.0f;
            }
            if (!g_odstCamera.motionBlurZeroed)
            {
                g_odstCamera.motionBlurZeroed = true;
                LOG("ODST comfort: motion blur forced OFF through title-native scale/max vars");
            }
        }
        else if (g_odstCamera.motionBlurZeroed)
        {
            for (OdstMotionBlurVar& var : g_odstCamera.motionBlurVars)
                *var.slot = var.original;
            g_odstCamera.motionBlurZeroed = false;
            LOG("ODST comfort: motion blur restored to engine values");
        }
    }

    void RestoreOdstMotionBlurVars()
    {
        if (!g_odstCamera.motionBlurResolved || !g_odstCamera.motionBlurZeroed)
            return;
        for (OdstMotionBlurVar& var : g_odstCamera.motionBlurVars)
            *var.slot = var.original;
        g_odstCamera.motionBlurZeroed = false;
        LOG("ODST comfort: stock motion-blur values restored during teardown");
    }
} // namespace
namespace odst_bridge
{
    unsigned char* OdstLoadedTagDefinition(uint32_t datum)
    {
        void** instSlot = g_odstTagInstanceTable;
        void** baseSlot = g_odstTagDataBase;
        // Both globals live in the zero-init tail of .data: their CONTENTS are
        // only valid once the engine has loaded a map, so a null here
        // is normal at menu, not a fault.
        auto* instances = instSlot ? static_cast<unsigned char*>(*instSlot) : nullptr;
        auto* tagBase = baseSlot ? static_cast<unsigned char*>(*baseSlot) : nullptr;
        const uint32_t index = datum & 0xFFFFu;
        if (!instances || !tagBase || index == 0xFFFFu)
            return nullptr;
        const uint32_t address = *reinterpret_cast<const uint32_t*>(instances + static_cast<size_t>(index) * 8 + 4);
        return address ? tagBase + static_cast<size_t>(address) * 4 : nullptr;
    }

    constinit OdstVehicleTransform g_odstVehicleTransform;
} // namespace odst_bridge
namespace
{
    constinit std::atomic<uint64_t> g_odstVehicleSampleMs{0};
    // Seated in the intro drop pod (OdstIsDropPodSeat): last sample time and
    // generation, 0 when not. Read by the aim servo.
    constinit std::atomic<uint64_t> g_odstDropPodSeatMs{0};
    constinit std::atomic<uint32_t> g_odstDropPodSeatGeneration{0};
    constinit std::atomic<bool> g_odstDropPodSeatLogged{false};
    constinit std::atomic<uint64_t> g_odstDropPodSeatLogMs{0};
    constinit std::atomic<uint32_t> g_odstDropPodSeatLogSuppressed{0};
    // Pod look-around: proven action-test accumulator (see the sigs), the
    // aim servo's stick frames while in the pod (diagnostic), and the bits
    // head look completed.
    constinit std::atomic<bool> g_odstLookTestProven{false};
} // namespace
namespace odst_bridge
{
    constinit std::atomic<uint32_t> g_odstPodServoFrames[4]{}; // stick up/down/left/right
} // namespace odst_bridge
namespace
{
    // Pod look feed (InstallOdstPodLookFeed): polls seen by the two reader
    // hooks, and what the builder hook fed.
    constinit head_look_test::PollTracker g_odstLookPolls;
    constinit std::atomic<bool> g_odstPodLookFeedReady{false};
    constinit std::atomic<uint64_t> g_odstPodLookFedBits{0}, g_odstPodLookFedTicks{0};
    void OdstPodLookTick(bool podSeat, uint64_t nowMs);
} // namespace
namespace odst_bridge
{
    constinit std::atomic<uint64_t> g_odstRoomscaleOnFootMs{0};
    constinit std::atomic<uint32_t> g_odstVehicleFpStable{static_cast<uint32_t>(Halo3VehicleState::Unknown)};
} // namespace odst_bridge
namespace
{
    constinit Halo3VehicleDebounce g_odstVehicleFpDebounce;
    constinit std::atomic<uint32_t> g_odstSeatRecenters{0};

    // Camera-thread caches (plain: only the camera thread touches them).
    struct OdstSeatNodeCache
    {
        uint32_t generation = 0;
        int objectHandle = -1;
        uint32_t definitionIndex = 0xFFFFFFFFu;
        int nodeIndex = -1;
        int seatIndex = -1;
        uint32_t vehicleId = static_cast<uint32_t>(OdstVehicleId::Unknown);
        bool mounted = false;
        bool resolved = false;
        bool valid = false;
        Halo3Matrix4x3 defaultInverse{};
    };
    constinit OdstSeatNodeCache g_odstSeatNodeCache;

    struct OdstHeadReference
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
    constinit OdstHeadReference g_odstHeadReference;

#include "odst_native_seat_patch.inl"

    // The authored point in the seat node's local space: tag point + trims,
    // through the render model's stored default inverse for that node.
    bool OdstResolveAuthoredNodeLocal(uint32_t generation, int objectHandle, uint32_t definitionIndex, int nodeIndex,
        OdstVehicleId vehicleId, int seatIndex, bool mounted, float trimForward, float trimUp, float trimRight,
        float outNodeLocal[3])
    {
        const OdstSeatPoint* point = OdstFindSeatPoint(vehicleId, seatIndex, mounted);
        if (!point || nodeIndex < 0 || definitionIndex > 0xFFFFu)
            return false;
        OdstSeatNodeCache& cache = g_odstSeatNodeCache;
        const bool same = cache.resolved && cache.generation == generation && cache.objectHandle == objectHandle &&
                          cache.definitionIndex == definitionIndex && cache.nodeIndex == nodeIndex &&
                          cache.seatIndex == seatIndex && cache.vehicleId == static_cast<uint32_t>(vehicleId) &&
                          cache.mounted == mounted;
        const auto emit = [&]()
        {
            if (!cache.valid)
                return false;
            // Tag Y is LEFT, so a rightward trim subtracts. Same convention as
            // the accepted Halo 3 path.
            const float authored[3] = {point->x + trimForward, point->y - trimRight, point->z + trimUp};
            return Halo3MatrixTransformPoint(cache.defaultInverse, authored, outNodeLocal);
        };
        if (same)
            return emit();

        cache = {};
        cache.generation = generation;
        cache.objectHandle = objectHandle;
        cache.definitionIndex = definitionIndex;
        cache.nodeIndex = nodeIndex;
        cache.seatIndex = seatIndex;
        cache.vehicleId = static_cast<uint32_t>(vehicleId);
        cache.mounted = mounted;
        cache.resolved = true; // a failed walk is cached, never retried
        bool readOk = false;
        __try
        {
            unsigned char* objectDef = OdstLoadedTagDefinition(definitionIndex);
            const uint32_t modelDatum =
                objectDef ? *reinterpret_cast<const uint32_t*>(objectDef + kOdstVehicleModelRefOffset) : 0xFFFFFFFFu;
            unsigned char* modelDef = OdstLoadedTagDefinition(modelDatum);
            const uint32_t renderDatum =
                modelDef ? *reinterpret_cast<const uint32_t*>(modelDef + kOdstModelRenderRefOffset) : 0xFFFFFFFFu;
            unsigned char* renderDef = OdstLoadedTagDefinition(renderDatum);
            void** baseSlot = g_odstTagDataBase;
            auto* tagBase = baseSlot ? static_cast<unsigned char*>(*baseSlot) : nullptr;
            if (renderDef && tagBase)
            {
                const int32_t nodeCount =
                    *reinterpret_cast<const int32_t*>(renderDef + kOdstRenderModelNodesCountOffset);
                const uint32_t nodesAddress =
                    *reinterpret_cast<const uint32_t*>(renderDef + kOdstRenderModelNodesDataOffset);
                if (nodeCount > 0 && nodeIndex < nodeCount && nodesAddress)
                {
                    unsigned char* node = tagBase + static_cast<size_t>(nodesAddress) * 4 +
                                          static_cast<size_t>(nodeIndex) * kOdstRenderModelNodeStride;
                    memcpy(
                        &cache.defaultInverse, node + kOdstRenderModelNodeInverseOffset, sizeof(cache.defaultInverse));
                    readOk = true;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("resolving an ODST authored node faulted");
            readOk = false;
        }
        cache.valid = readOk && Halo3MatrixValid(cache.defaultInverse);
        return emit();
    }

    // One world-space node matrix for the seat/attachment node, preferring the
    // engine's own interpolated bank so the eye rides the RENDERED vehicle.
    bool OdstReadLiveNodeMatrix(
        int objectHandle, unsigned char* objectData, int nodeIndex, Halo3Matrix4x3& out, bool& outInterpolated)
    {
        outInterpolated = false;
        if (nodeIndex < 0 || !objectData)
            return false;
        bool accessorReturned = false;
        if (g_config.vehicle_cam_smoothing &&
            g_odstNodeBinding.load(std::memory_order_acquire) ==
                static_cast<uint8_t>(OdstVehicleBindingState::Installed) &&
            g_odstInterpolatedNodes)
        {
            __try
            {
                Halo3Matrix4x3* matrices = nullptr;
                int count = 0;
                accessorReturned =
                    g_odstInterpolatedNodes(objectHandle, &matrices, &count) != 0 && matrices && nodeIndex < count;
                if (accessorReturned)
                {
                    out = matrices[nodeIndex];
                    outInterpolated = true;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                GUARD_COUNT("reading the ODST interpolated nodes faulted");
                // Losing the accessor costs smoothing, never the seat: fall
                // through to the raw bank and stop calling it.
                g_odstNodeBinding.store(
                    static_cast<uint8_t>(OdstVehicleBindingState::StockFallback), std::memory_order_release);
                accessorReturned = false;
                outInterpolated = false;
            }
        }
        if (!accessorReturned)
        {
            __try
            {
                const int byteSize = *reinterpret_cast<const int16_t*>(objectData + kOdstObjectNodeBankSizeOffset);
                const int count = Halo3MatrixCountFromByteSize(static_cast<uint16_t>(byteSize));
                const int relative = *reinterpret_cast<const int16_t*>(objectData + kOdstObjectNodeBankRelOffset);
                if (count <= 0 || nodeIndex >= count || relative == 0)
                    return false;
                memcpy(
                    &out, objectData + relative + static_cast<size_t>(nodeIndex) * kOdstNodeMatrixStride, sizeof(out));
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                GUARD_COUNT("reading the ODST object node bank faulted");
                return false;
            }
        }
        return Halo3MatrixValid(out);
    }

    // The occupant's animated head marker, in world space. Optional: losing it
    // costs the seat bounce, never the seat.
    bool OdstReadPhaseMatchedHead(int unitHandle, bool interpolateNodes, float outWorld[3])
    {
        if (g_odstNodeBinding.load(std::memory_order_acquire) !=
                static_cast<uint8_t>(OdstVehicleBindingState::Installed) ||
            !g_odstMarkersInternal)
            return false;
        bool ok = false;
        __try
        {
            Halo3ObjectMarker marker{};
            const int16_t found = g_odstMarkersInternal(
                unitHandle, static_cast<int>(kOdstHeadMarkerStringId), &marker, 1, false, interpolateNodes);
            if (found > 0)
            {
                outWorld[0] = marker.matrix.position[0];
                outWorld[1] = marker.matrix.position[1];
                outWorld[2] = marker.matrix.position[2];
                ok = std::isfinite(outWorld[0]) && std::isfinite(outWorld[1]) && std::isfinite(outWorld[2]);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading the ODST head marker faulted");
            g_odstNodeBinding.store(
                static_cast<uint8_t>(OdstVehicleBindingState::StockFallback), std::memory_order_release);
            ok = false;
        }
        return ok;
    }
} // namespace
namespace odst_bridge
{
    bool OdstReadSeatSnapshot(OdstSeatSnapshot& out)
    {
        OdstVehicleTransform& xf = g_odstVehicleTransform;
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const uint32_t before = xf.seq.load(std::memory_order_acquire);
            if (before & 1u)
                continue;
            if (!xf.valid.load(std::memory_order_relaxed))
                return false;
            const uint64_t sampleMs = xf.sampleMs.load(std::memory_order_relaxed);
            OdstSeatSnapshot seat{};
            memcpy(seat.rawFwd, xf.rawFwd, sizeof(seat.rawFwd));
            memcpy(seat.anchor, xf.anchor, sizeof(seat.anchor));
            memcpy(seat.anchorBase, xf.anchorBase, sizeof(seat.anchorBase));
            seat.anchorValid = xf.anchorValid != 0;
            seat.anchorBaseValid = xf.anchorBaseValid != 0;
            seat.seatIndex = xf.seatIndex;
            seat.parentHandle = xf.parentHandle;
            seat.identity = static_cast<OdstVehicleId>(xf.vehicleId);
            seat.mounted = xf.mountedTurret != 0;
            if (xf.seq.load(std::memory_order_acquire) != before)
                continue;
            const uint64_t nowMs = GetTickCount64();
            if (!sampleMs || nowMs < sampleMs || nowMs - sampleMs > 100)
                return false;
            for (float v : {seat.anchor[0], seat.anchor[1], seat.anchor[2], seat.anchorBase[0], seat.anchorBase[1],
                     seat.anchorBase[2], seat.rawFwd[0], seat.rawFwd[1], seat.rawFwd[2]})
                if (!std::isfinite(v))
                    return false;
            out = seat;
            return true;
        }
        return false;
    }

    bool OdstVehicleFpActive()
    {
        if (!g_config.vehicle_first_person)
            return false;
        if (g_odstVehicleBinding.load(std::memory_order_acquire) !=
            static_cast<uint8_t>(OdstVehicleBindingState::Installed))
            return false;
        if (g_odstVehicleFpStable.load(std::memory_order_relaxed) != static_cast<uint32_t>(Halo3VehicleState::Vehicle))
            return false;
        const uint64_t sampleMs = g_odstVehicleSampleMs.load(std::memory_order_relaxed);
        const uint64_t nowMs = GetTickCount64();
        return sampleMs && nowMs >= sampleMs && nowMs - sampleMs <= 500;
    }
} // namespace odst_bridge
namespace
{
    bool OdstComputeAuthoredAnchor(float out[3])
    {
        OdstSeatSnapshot seat;
        if (!OdstReadSeatSnapshot(seat) || !seat.anchorValid)
            return false;
        memcpy(out, seat.anchor, sizeof(seat.anchor));
        return true;
    }

    // True only in a seat where the occupant fires their OWN weapon, which is
    // the only case whose projectiles leave the occupant's eye point and are
    // therefore displaced by our seat camera. A driver's or a mounted gunner's
    // shots come out of a vehicle barrel and must not be re-aimed.
    bool OdstSeatFiresPersonalWeapon()
    {
        if (!OdstVehicleFpActive())
            return false;
        OdstVehicleTransform& xf = g_odstVehicleTransform;
        if (!xf.valid.load(std::memory_order_relaxed))
            return false;
        return (xf.occupiedSeatFlags & kOdstSeatAllowsWeaponsBit) != 0;
    }

    // Hold the engine's exposure steady while a seat owns the view. Looking
    // down at a dashboard fills the luminance measurement with a large dark
    // surface, so the eye-adaptation integrator ramps the whole scene's
    // exposure -- something flat play never provokes because nobody looks at
    // the dash. This is the engine's own lock, so authored/scripted cinematic
    // exposure changes deliberately bypass it and still work.
    //
    // ASSERTED, never restored from a captured value: this is a module global
    // that outlives our camera generation, and the engine's per-view save and
    // restore copies the whole render-debug block around it. The stock value
    // is the known constant 0, so leaving a seat writes that constant back
    // rather than anything we remembered.
    void OdstApplyExposureLock(bool engage)
    {
        if (!g_odstExposureLock && !g_odstExposureEditableLock)
            return;
        uint8_t observed = 0xFFu;
        __try
        {
            // Stock is 0 and held is 1 in BOTH copies. Write the internal one
            // first: it is the copy the engine rewrites every frame, so if the
            // editable write faults we are left latched on the self-healing
            // copy rather than the permanent one.
            const uint8_t want = engage ? 1u : 0u;
            if (g_odstExposureLock)
            {
                observed = *g_odstExposureLock;
                if (observed != want)
                    *g_odstExposureLock = want;
            }
            // The editable block is the SOURCE main_render rebuilds the
            // internal block from, and nothing in the module ever writes it
            // but us. That makes this write stick -- which is the whole point,
            // and also why every teardown path must release it.
            if (g_odstExposureEditableLock && *g_odstExposureEditableLock != want)
            {
                *g_odstExposureEditableLock = want;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("applying the ODST exposure lock faulted");
            g_odstExposureLock = nullptr;
            g_odstExposureEditableLock = nullptr;
            LOG("ODST vehicle exposure: FAULTED writing an adaptation switch; "
                "exposure control abandoned, everything else unaffected");
            return;
        }
        // One frame after engaging, `observed` is what the engine carried into
        // this frame. If the hold is real it reads 1; if something still
        // rebuilds it from stock it reads 0 and we say so once, loudly.
        if (engage && g_odstExposureVerifyPending.load(std::memory_order_relaxed) && observed != 0xFFu)
        {
            g_odstExposureVerifyPending.store(0, std::memory_order_relaxed);
            g_odstExposureVerified.store(observed == 1u ? 1u : 2u, std::memory_order_relaxed);
            if (observed != 1u)
            {
                LOG("ODST vehicle exposure: the hold is NOT taking -- the "
                    "engine's own copy read %u a frame after we engaged, so "
                    "something still rebuilds it; auto-exposure is effectively "
                    "still the engine's",
                    (unsigned)observed);
            }
            else
            {
                LOG("ODST vehicle exposure: hold verified against the engine's "
                    "own copy (reads 1 a frame after engaging)");
            }
        }
        const uint32_t state = engage ? 1u : 0u;
        if (g_odstExposureLockState.exchange(state, std::memory_order_relaxed) != state)
        {
            // The engage COUNT is the diagnostic that matters: a steady ride
            // must show one engage, not a stream of them. A stream would mean
            // something is still releasing the lock mid-ride.
            const uint32_t engages = engage ? g_odstExposureEngages.fetch_add(1, std::memory_order_relaxed) + 1
                                            : g_odstExposureEngages.load(std::memory_order_relaxed);
            if (engage)
            {
                g_odstExposureVerifyPending.store(1, std::memory_order_relaxed);
                g_odstExposureVerified.store(0, std::memory_order_relaxed);
            }
            LOG("ODST vehicle exposure: adaptation %s (engage #%u; internal "
                "copy %s, editable copy %s)",
                engage ? "HELD steady for the seat view" : "returned to the engine's own control", engages,
                g_odstExposureLock ? "on" : "unavailable", g_odstExposureEditableLock ? "on" : "unavailable");
        }
    }

    // Teardown release. This is a prerequisite, not tidiness: nothing in the
    // module ever writes the editable copy but us, and halo3odst.dll stays
    // resident after a title exit, so an engage left latched would freeze
    // auto-exposure for the menus, on-foot play and the next level. Release
    // first, then drop the pointers, so we can never write through an address
    // whose module has gone.
    void ReleaseOdstExposureLock()
    {
        if (g_odstExposureLock || g_odstExposureEditableLock)
            OdstApplyExposureLock(false);
        g_odstExposureLock = nullptr;
        g_odstExposureEditableLock = nullptr;
        g_odstExposureLockState.store(0, std::memory_order_relaxed);
        g_odstExposureVerifyPending.store(0, std::memory_order_relaxed);
        g_odstExposureVerified.store(0, std::memory_order_relaxed);
    }

    // The shot-origin hook must come out with the module. Its detour reads
    // only our own atomics, but the trampoline points into halo3odst.dll and
    // that module is released on teardown.
    void ReleaseOdstFiringOriginHook()
    {
        g_odstFiringOriginReturn = 0;
        g_odstSeatedPlayerUnit.store(-1, std::memory_order_relaxed);
        if (!g_odstUnitCameraPositionTarget)
            return;
        MH_DisableHook(g_odstUnitCameraPositionTarget);
        MCCVR_RemoveHookChecked(g_odstUnitCameraPositionTarget);
        g_odstUnitCameraPositionTarget = nullptr;
        g_origOdstUnitCameraPosition = nullptr;
    }

    // T-2 shot event for ODST. The firing-origin hook carries no direction of
    // its own (the engine keeps the barrel/aiming-vector direction), so this
    // event is direction-ABSENT by construction: an exact zero direction,
    // documented, never a fabricated ray. The origin is the final value the
    // engine will consume from the firing call-site's own buffer (ours when the
    // substitution wrote it, the engine's otherwise). Slot/barrel/weapon and
    // predicted are not carried by this call-site, so they stay unknown
    // (kTelemetryShotIndexUnknown / UINT32_MAX envelope sentinels); a
    // non-finite origin is suppressed rather than published.
    void PublishOdstShotDiagnostic(int32_t unitIndex, const float finalOrigin[3], bool substituted)
    {
        const float absentDirection[3] = {0.0f, 0.0f, 0.0f};
        PublishShotDiagnostic(GameTitle::Halo3ODST, g_odstRuntimeGeneration.load(std::memory_order_acquire),
            static_cast<uint32_t>(unitIndex), UINT32_MAX, kTelemetryShotIndexUnknown, kTelemetryShotIndexUnknown,
            /*predicted=*/false, substituted, /*firesFromCamera=*/false,
            /*unitAim=*/false, finalOrigin, absentDirection);
    }

    // Runs on the engine's own weapon path. Everything here is a relaxed load
    // of our own state plus three float stores: no locks, no allocation, no
    // logging, no engine reads beyond the point the original just wrote.
    __declspec(noinline) void __fastcall OdstUnitCameraPositionHook(int32_t unitIndex, float* out, uint8_t flags)
    {
        // T-2 shot gate FIRST: one atomic load, before any diagnostic read.
        // The substitution below is gameplay and always runs; this only
        // decides whether the same firing invocation also publishes an event.
        const bool telemetryAccepting = Telemetry_WeaponEventsAccepting();
        // Capture before the call: this is the site that decides whether we
        // are on the firing path or on one of the 35 other consumers.
        const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
        // Always let the engine produce its own answer first. Several paths
        // through this function return without writing, and callers pass
        // uninitialised stack, so we must never substitute a value for a
        // result the engine declined to give.
        g_origOdstUnitCameraPosition(unitIndex, out, flags);
        if (!out)
            return;
        const uintptr_t firingReturn = g_odstFiringOriginReturn;
        if (!firingReturn || caller != firingReturn)
            return; // not the shot line; leave it alone
        if (g_odstCinematicLocked.load(std::memory_order_relaxed))
        {
            // Authored camera owns the view; the engine's own shot origin is
            // still the ray this shot will consume, so it is reported stock.
            if (telemetryAccepting)
                PublishOdstShotDiagnostic(unitIndex, out, false);
            return;
        }
        const int32_t seated = g_odstSeatedPlayerUnit.load(std::memory_order_relaxed);
        // Full salted identity rejects a recycled unit slot after a checkpoint.
        if (seated == -1 || seated != unitIndex)
        {
            if (telemetryAccepting)
                PublishOdstShotDiagnostic(unitIndex, out, false);
            return; // somebody else's weapon
        }
        if (!OdstSeatFiresPersonalWeapon())
        {
            if (telemetryAccepting)
                PublishOdstShotDiagnostic(unitIndex, out, false);
            return; // driver/mounted gun fires from a barrel
        }
        if (!g_camValid.load(std::memory_order_acquire))
        {
            if (telemetryAccepting)
                PublishOdstShotDiagnostic(unitIndex, out, false);
            return;
        }
        const float eyeX = g_camX.load(std::memory_order_relaxed);
        const float eyeY = g_camY.load(std::memory_order_relaxed);
        const float eyeZ = g_camZ.load(std::memory_order_relaxed);
        if (!isfinite(eyeX) || !isfinite(eyeY) || !isfinite(eyeZ))
        {
            g_odstFiringOriginSkips.fetch_add(1, std::memory_order_relaxed);
            if (telemetryAccepting)
                PublishOdstShotDiagnostic(unitIndex, out, false);
            return;
        }
        out[0] = eyeX;
        out[1] = eyeY;
        out[2] = eyeZ;
        g_odstFiringOriginMoves.fetch_add(1, std::memory_order_relaxed);
        if (telemetryAccepting)
            PublishOdstShotDiagnostic(unitIndex, out, true);
    }
} // namespace
namespace odst_bridge
{
    // The follow gate for the shared reader. ODST answers only when its
    // own transaction is armed and fresh, so a Halo 3 sample can never pitch
    // an ODST camera or the reverse.
    bool OdstFollowContextValid(uint32_t& outGeneration)
    {
        if (!OdstVehicleFpActive())
            return false;
        const uint32_t generation = g_odstRuntimeGeneration.load(std::memory_order_acquire);
        if (!generation)
            return false;
        outGeneration = generation;
        return true;
    }
} // namespace odst_bridge
namespace
{
    // Camera-thread follow state, mirroring Halo 3's.
    constinit Halo3SeatYaw g_odstSeatYaw;
    constinit Halo3SeatPositionKey g_odstYawSeat;
    constinit std::atomic<uint32_t> g_odstSeatAuthorsSteering{0};

    // Integrate the rendered hull's yaw into the VR heading reference and
    // publish its roll-free pitch. Runs where Halo 3 runs its equivalent: on
    // the camera thread, before the head look that consumes it.
    void OdstApplySeatYawFollow()
    {
        OdstSeatSnapshot seat{};
        const bool fpActive = OdstVehicleFpActive();
        const bool haveSeat = fpActive && OdstReadSeatSnapshot(seat) && seat.anchorValid;
        const bool active = haveSeat && OdstSeatFollowsHull(seat.identity, seat.seatIndex, seat.mounted);
        const bool odstDriver = haveSeat && OdstSeatIsDriver(seat.identity, seat.seatIndex, seat.mounted);
        vehicle_wheel::SteerClass odstClass = vehicle_wheel::SteerClass::None;
        if (haveSeat && OdstVehicleUsesWheel(seat.identity))
            odstClass = seat.identity == OdstVehicleId::Mongoose || seat.identity == OdstVehicleId::Ghost ||
                                seat.identity == OdstVehicleId::Chopper
                            ? vehicle_wheel::SteerClass::Bike
                            : vehicle_wheel::SteerClass::Car;
        if (TitleAdapter_GetActiveTitle() == GameTitle::Halo3ODST) // on foot publishes "not the driver"
            PublishVehicleSteerContext(GameTitle::Halo3ODST, fpActive && odstDriver, odstClass,
                seat.identity == OdstVehicleId::Ghost || seat.identity == OdstVehicleId::Chopper,
                haveSeat && OdstVehicleIsLookSteered(seat.identity) && !OdstVehicleUsesWheel(seat.identity));
        const bool followEnabled = vehicle_wheel::EffectiveFollow(g_config.vehicle_view_follow, odstDriver, odstClass,
            vehicle_wheel::ModeFor(odstClass, g_config.vehicle_steer_car, g_config.vehicle_steer_bike));
        const bool pitchEligible = !g_config.vehicle_horizon_lock && active &&
                                   OdstSeatFollowsPitch(seat.identity, seat.seatIndex, seat.mounted);
        const uint32_t generation = g_odstRuntimeGeneration.load(std::memory_order_acquire);

        if (!fpActive || !followEnabled || !active)
        {
            // OFF, on foot, or a seat that must not follow (the Shade, whose
            // own body is what its aim turns) reaches the established
            // world-locked path untouched.
            if (g_odstSeatYaw.armed)
                g_odstSeatYaw = {};
            g_odstYawSeat = {};
            g_odstSeatAuthorsSteering.store(0, std::memory_order_release);
            Halo3ClearRollStableFollow();
            return;
        }
        if (!g_odstYawSeat.Matches(
                generation, seat.parentHandle, seat.seatIndex, static_cast<uint32_t>(seat.identity), seat.mounted))
        {
            // A new seat starts a new integration; never carry a heading
            // across vehicles.
            Halo3ClearRollStableFollow();
            g_odstYawSeat.Set(
                generation, seat.parentHandle, seat.seatIndex, static_cast<uint32_t>(seat.identity), seat.mounted);
            g_odstSeatYaw = {};
        }
        // Fed the RAW rendered hull forward, never a smoothed or predicted
        // one: the follow INTEGRATES, so any cleverness here accumulates.
        const float delta = Halo3SeatYawDelta(g_odstSeatYaw, seat.rawFwd, true);
        NoteVehicleFollowYaw(delta);
        if (delta != 0.0f)
            g_gameYawRef = Halo3WrapPi(g_gameYawRef + delta);

        float hullPitch = 0.0f;
        if (pitchEligible && g_odstSeatYaw.armed && Halo3RollStablePitchFromForward(seat.rawFwd, hullPitch))
        {
            Halo3PublishRollStableFollowFor(generation, VehicleFollowOwner::Odst, g_odstSeatYaw.lastYaw, hullPitch);
        }
        else
        {
            // Aircraft keep the proven yaw-only path: their flight crosses
            // vertical, where forward alone cannot pick a roll-free up.
            Halo3ClearRollStableFollow();
        }

        const bool authors = OdstSeatAuthorsSteering(seat.identity, seat.seatIndex, seat.mounted, followEnabled);
        if (!authors)
        {
            g_halo3SteerAxis.store(0.0f, std::memory_order_relaxed);
            g_halo3WheelDriving.store(0, std::memory_order_relaxed);
        }
        g_odstSeatAuthorsSteering.store(authors ? 1u : 0u, std::memory_order_release);
    }
} // namespace
namespace odst_bridge
{
    // Same live re-check Halo 3 performs: the turn and aim authors are shared
    // across titles, so a latched flag alone must never suppress another
    // title's stick.
    bool OdstSeatAuthorsSteeringNow()
    {
        return g_odstSeatAuthorsSteering.load(std::memory_order_acquire) != 0 && OdstVehicleFpActive();
    }

    bool OdstSeatUsesWheel()
    {
        if (!OdstSeatAuthorsSteeringNow())
            return false;
        OdstSeatSnapshot seat;
        if (!OdstReadSeatSnapshot(seat))
            return false;
        return OdstVehicleUsesWheel(seat.identity);
    }

    // The pre-bounce placement: rigid in the vehicle's frame, so the arms and
    // gun ride the seat rather than the occupant's animated head.
    bool OdstComputeSeatBodyAnchor(float out[3])
    {
        if (!out || !OdstVehicleFpActive())
            return false;
        OdstSeatSnapshot seat;
        if (!OdstReadSeatSnapshot(seat) || !seat.anchorBaseValid)
            return false;
        memcpy(out, seat.anchorBase, sizeof(seat.anchorBase));
        return true;
    }
} // namespace odst_bridge
namespace
{
    void OdstSampleVehicleState(uint64_t nowMs)
    {
        if (g_odstVehicleBinding.load(std::memory_order_acquire) !=
                static_cast<uint8_t>(OdstVehicleBindingState::Installed) ||
            !g_odstEngineTlsIndex)
            return;
        const uint32_t generation = g_odstRuntimeGeneration.load(std::memory_order_acquire);
        if (!generation)
            return;
        // Seated: sample every camera frame so the seat tracks the rendered
        // vehicle. On foot a 15 ms latch is plenty.
        const bool seatedLastSample = g_odstVehicleTransform.valid.load(std::memory_order_relaxed) != 0;
        const uint64_t last = g_odstVehicleSampleMs.load(std::memory_order_relaxed);
        if (!seatedLastSample && nowMs - last < 15)
            return;
        g_odstVehicleSampleMs.store(nowMs, std::memory_order_relaxed);

        int unit = -1;
        int seat = -1;
        int parent = -1;
        unsigned parentKind = 0;
        unsigned native = 0;
        uint32_t defIndex = 0xFFFFFFFFu;
        int physicsType = -1;
        int unitParentNode = -1;
        unsigned char* parentObjectData = nullptr;
        uint32_t seat0Flags = 0;
        uint32_t occupiedSeatFlags = 0;
        bool seatFlagsValid = false;
        int32_t seatCountRead = 0;
        bool faulted = false;
        bool unseatedProven = false;
        __try
        {
            auto** slots = reinterpret_cast<void**>(__readgsqword(0x58));
            auto* tls = slots ? reinterpret_cast<unsigned char*>(slots[*g_odstEngineTlsIndex]) : nullptr;
            unsigned char* table = tls ? *reinterpret_cast<unsigned char**>(tls + kOdstTlsObjectTableOffset) : nullptr;
            unsigned char* entries =
                table ? *reinterpret_cast<unsigned char**>(table + kOdstObjectTableEntriesOffset) : nullptr;
            unit = g_odstPlayerUnitGetter(0);
            if (unit != -1 && entries)
            {
                unsigned char* data = *reinterpret_cast<unsigned char**>(
                    entries + static_cast<size_t>(unit & 0xFFFF) * kOdstObjectEntryStride + kOdstObjectEntryDataOffset);
                if (data)
                {
                    seat = *reinterpret_cast<const int16_t*>(data + kOdstUnitSeatWordOffset);
                    unseatedProven = OdstSeatWordMeansUnseated(seat);
                    if (!OdstSeatWordMeansUnseated(seat))
                    {
                        native = g_odstUnitInVehicle(unit);
                        unitParentNode = *reinterpret_cast<const int8_t*>(data + kOdstObjectParentNodeOffset);
                        parent = *reinterpret_cast<const int32_t*>(data + kOdstObjectParentOffset);
                        if (parent != -1)
                        {
                            unsigned char* parentEntry =
                                entries + static_cast<size_t>(parent & 0xFFFF) * kOdstObjectEntryStride;
                            parentKind = *(parentEntry + kOdstObjectEntryKindOffset);
                            unsigned char* parentData =
                                *reinterpret_cast<unsigned char**>(parentEntry + kOdstObjectEntryDataOffset);
                            if (parentData && parentKind == kOdstObjectKindVehicle)
                            {
                                parentObjectData = parentData;
                                const uint16_t definitionIndex = *reinterpret_cast<const uint16_t*>(parentData);
                                defIndex = definitionIndex;
                                physicsType = g_odstVehicleTypeAccessor(definitionIndex);
                                unsigned char* definition = OdstLoadedTagDefinition(definitionIndex);
                                void** baseSlot = g_odstTagDataBase;
                                auto* tagBase = baseSlot ? static_cast<unsigned char*>(*baseSlot) : nullptr;
                                if (definition && tagBase)
                                {
                                    const int32_t seatCount =
                                        *reinterpret_cast<const int32_t*>(definition + kOdstVehicleSeatsCountOffset);
                                    const uint32_t seatsAddress =
                                        *reinterpret_cast<const uint32_t*>(definition + kOdstVehicleSeatsDataOffset);
                                    if (seatCount > 0 && seatCount <= 126 && seatsAddress)
                                    {
                                        unsigned char* seats = tagBase + static_cast<size_t>(seatsAddress) * 4;
                                        seat0Flags = *reinterpret_cast<const uint32_t*>(seats + kOdstSeatFlagsOffset);
                                        seatFlagsValid = true;
                                        seatCountRead = seatCount;
                                        if (seat >= 0 && seat < seatCount)
                                        {
                                            occupiedSeatFlags = *reinterpret_cast<const uint32_t*>(
                                                seats + static_cast<size_t>(seat) * kOdstVehicleSeatStride +
                                                kOdstSeatFlagsOffset);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("sampling the ODST vehicle state faulted");
            faulted = true;
        }

        if (faulted)
        {
            // A fault here means an offset is wrong for this build. Disarm the
            // whole ODST vehicle transaction loudly rather than keep reading.
            OdstRestoreNativeSeatPatch();
            g_odstVehicleTransform.valid.store(0, std::memory_order_relaxed);
            g_odstVehicleFpStable.store(static_cast<uint32_t>(Halo3VehicleState::Unknown), std::memory_order_relaxed);
            g_odstVehicleBinding.store(
                static_cast<uint8_t>(OdstVehicleBindingState::StockFallback), std::memory_order_release);
            g_odstVehicleProbeFaults.fetch_add(1, std::memory_order_relaxed);
            LOG("ODST vehicle seat: FAULTED reading engine state; the ODST "
                "vehicle transaction is disarmed and every vehicle camera "
                "returns to stock. The ODST camera core is unaffected");
            return;
        }

        g_odstRoomscaleOnFootMs.store(unit != -1 && unseatedProven ? nowMs : 0, std::memory_order_release);
        const bool seated = unit != -1 && !OdstSeatWordMeansUnseated(seat) && parentObjectData != nullptr;

        // Identity. A mounted-turret gunner is not resolved in this candidate:
        // ODST's object parent-node byte is not yet proven, so
        // an attached gun keeps the stock chase view rather than guessing the
        // carrier frame.
        OdstDefinitionFields fields{};
        fields.physicsType = physicsType;
        fields.seatFlagsValid = seatFlagsValid;
        fields.seat0Flags = seat0Flags;
        fields.inVehicle = native != 0;
        if (physicsType == 1)
        {
            // Jeep cousins are separated by engine moment, read from the
            // definition's own physics record.
            __try
            {
                unsigned char* definition = OdstLoadedTagDefinition(defIndex);
                void** baseSlot = g_odstTagDataBase;
                auto* tagBase = baseSlot ? static_cast<unsigned char*>(*baseSlot) : nullptr;
                if (definition && tagBase)
                {
                    unsigned char* block = definition + kOdstVehiclePhysicsBlocksOffset +
                                           1 * kOdstVehiclePhysicsRecordStride; // human_jeep
                    const uint32_t address = *reinterpret_cast<const uint32_t*>(block + 4);
                    if (address)
                    {
                        const unsigned char* element = tagBase + static_cast<size_t>(address) * 4;
                        fields.engineMoment = *reinterpret_cast<const float*>(element + kOdstJeepEngineMomentOffset);
                        fields.jeepValid = std::isfinite(fields.engineMoment);
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                GUARD_COUNT("reading the ODST vehicle definition faulted");
                fields.jeepValid = false;
            }
        }
        else if (physicsType == 3)
        {
            __try
            {
                unsigned char* definition = OdstLoadedTagDefinition(defIndex);
                void** baseSlot = g_odstTagDataBase;
                auto* tagBase = baseSlot ? static_cast<unsigned char*>(*baseSlot) : nullptr;
                if (definition && tagBase)
                {
                    unsigned char* block = definition + kOdstVehiclePhysicsBlocksOffset +
                                           3 * kOdstVehiclePhysicsRecordStride; // alien_scout
                    const uint32_t address = *reinterpret_cast<const uint32_t*>(block + 4);
                    if (address)
                    {
                        const unsigned char* element = tagBase + static_cast<size_t>(address) * 4;
                        fields.specificType = *reinterpret_cast<const int8_t*>(element + kOdstScoutSpecificTypeOffset);
                        fields.scoutValid = true;
                    }
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                GUARD_COUNT("reading the ODST seat definition faulted");
                fields.scoutValid = false;
            }
        }
        const OdstVehicleId anchorId = seated ? OdstResolveVehicleId(fields) : OdstVehicleId::Unknown;
        // Drop pod: native aim follows the head while seated in it (see
        // Game_ComputeAimStick), so the pod's look-at-the-lights steps see
        // where the player looks.
        const bool podSeat = seated && OdstIsDropPodSeat(fields, seatCountRead);
        g_odstDropPodSeatMs.store(podSeat ? nowMs : 0, std::memory_order_release);
        g_odstDropPodSeatGeneration.store(podSeat ? generation : 0, std::memory_order_release);
        OdstPodLookTick(podSeat, nowMs);
        // Flicker guard: at most one entered/left line per 2 s; transitions in
        // between are counted and reported with the next line.
        if (podSeat != g_odstDropPodSeatLogged.exchange(podSeat, std::memory_order_relaxed))
        {
            const uint64_t lastLog = g_odstDropPodSeatLogMs.load(std::memory_order_relaxed);
            if (lastLog && nowMs >= lastLog && nowMs - lastLog < 2000)
                g_odstDropPodSeatLogSuppressed.fetch_add(1, std::memory_order_relaxed);
            else
            {
                g_odstDropPodSeatLogMs.store(nowMs, std::memory_order_relaxed);
                LOG("ODST drop pod seat %s: def=0x%X type=%d seats=%d seat0Flags=0x%X engineMoment=%.1f (%s); "
                    "native aim %s (%u quicker changes not logged)",
                    podSeat ? "entered" : "left", (unsigned)defIndex, physicsType, seatCountRead, seat0Flags,
                    fields.engineMoment, fields.jeepValid ? "read" : "unread",
                    podSeat ? "follows the head" : "follows the controller again",
                    g_odstDropPodSeatLogSuppressed.exchange(0, std::memory_order_relaxed));
            }
        }

        if (seated)
            OdstEnsureFirstPersonSeatFlag(defIndex, seat, generation);
        else
            OdstRestoreNativeSeatPatch();

        float cameraAnchor[3] = {};
        float bodyAnchor[3] = {};
        float rawFwd[3] = {};
        bool anchorValid = false;
        bool headParented = false;
        bool nodeInterpolated = false;
        if (seated && anchorId != OdstVehicleId::Unknown)
        {
            const int trimSlot = ConfigOdstSeatTrimSlot(static_cast<int>(anchorId), seat, false);
            const float trimScale = g_worldScale.load(std::memory_order_relaxed);
            const float trimForward = ConfigOdstSeatCamForward(g_config, trimSlot) * trimScale;
            const float trimUp = ConfigOdstSeatCamUp(g_config, trimSlot) * trimScale;
            const float trimRight = ConfigOdstSeatCamRight(g_config, trimSlot) * trimScale;

            float authoredNodeLocal[3] = {};
            Halo3Matrix4x3 liveNode{};
            // The parent-node byte is a LEAD, not a proof (still unproven): it
            // must land inside the render model's own node table, and the
            // matrix it selects must validate, or nothing is placed. A wrong
            // offset therefore costs the seat, never the camera.
            const bool haveLiveNode =
                unitParentNode >= 0 && unitParentNode <= kOdstMaximumRenderNodes &&
                OdstResolveAuthoredNodeLocal(generation, parent, defIndex, unitParentNode, anchorId, seat, false,
                    trimForward, trimUp, trimRight, authoredNodeLocal) &&
                OdstReadLiveNodeMatrix(parent, parentObjectData, unitParentNode, liveNode, nodeInterpolated);
            if (haveLiveNode)
            {
                memcpy(rawFwd, liveNode.forward, sizeof(rawFwd));
                anchorValid = Halo3MatrixTransformPoint(liveNode, authoredNodeLocal, cameraAnchor);
                memcpy(bodyAnchor, cameraAnchor, sizeof(bodyAnchor));

                // Occupant bounce, relative to the settled pose in THIS seat.
                const bool stableSeat = g_odstVehicleFpStable.load(std::memory_order_relaxed) ==
                                        static_cast<uint32_t>(Halo3VehicleState::Vehicle);
                OdstHeadReference& headRef = g_odstHeadReference;
                const bool sameHeadSeat = headRef.generation == generation && headRef.unitHandle == unit &&
                                          headRef.parentHandle == parent && headRef.nodeObjectHandle == parent &&
                                          headRef.seatIndex == seat && headRef.nodeIndex == unitParentNode &&
                                          headRef.interpolateNodes == g_config.vehicle_cam_smoothing &&
                                          headRef.nodeInterpolated == nodeInterpolated;
                if (!sameHeadSeat || !stableSeat)
                {
                    headRef = {};
                    headRef.generation = generation;
                    headRef.unitHandle = unit;
                    headRef.parentHandle = parent;
                    headRef.nodeObjectHandle = parent;
                    headRef.seatIndex = seat;
                    headRef.nodeIndex = unitParentNode;
                    headRef.interpolateNodes = g_config.vehicle_cam_smoothing;
                    headRef.nodeInterpolated = nodeInterpolated;
                }
                float headWorld[3] = {};
                float currentHeadLocal[3] = {};
                if (anchorValid && stableSeat && OdstReadPhaseMatchedHead(unit, nodeInterpolated, headWorld) &&
                    Halo3MatrixInverseTransformPoint(liveNode, headWorld, currentHeadLocal))
                {
                    float headAnchor[3] = {};
                    if (headRef.settle.Update(nowMs, currentHeadLocal) &&
                        Halo3ComputeHeadParentedPoint(liveNode, headWorld, headRef.settle.reference, authoredNodeLocal,
                            g_config.vehicle_bounce, headAnchor))
                    {
                        memcpy(cameraAnchor, headAnchor, sizeof(cameraAnchor));
                        headParented = true;
                    }
                }
                else if (!headRef.settle.valid)
                {
                    headRef.settle = {};
                }
            }
            else
            {
                g_odstHeadReference = {};
            }
        }
        else
        {
            g_odstSeatNodeCache = {};
            g_odstHeadReference = {};
        }

        OdstVehicleTransform& xf = g_odstVehicleTransform;
        xf.seq.fetch_add(1, std::memory_order_acq_rel);
        if (seated && anchorValid)
        {
            memcpy(xf.rawFwd, rawFwd, sizeof(xf.rawFwd));
            memcpy(xf.anchor, cameraAnchor, sizeof(xf.anchor));
            memcpy(xf.anchorBase, bodyAnchor, sizeof(xf.anchorBase));
            xf.anchorValid = 1;
            xf.anchorBaseValid = 1;
            xf.headParented = headParented ? 1u : 0u;
            xf.seatIndex = seat;
            xf.parentHandle = parent;
            xf.vehicleId = static_cast<uint32_t>(anchorId);
            xf.mountedTurret = 0;
            xf.occupiedSeatFlags = occupiedSeatFlags;
            xf.valid.store(1, std::memory_order_relaxed);
            xf.sampleMs.store(nowMs, std::memory_order_relaxed);
        }
        else
        {
            xf.anchorValid = 0;
            xf.anchorBaseValid = 0;
            xf.headParented = 0;
            xf.valid.store(0, std::memory_order_relaxed);
        }
        xf.seq.fetch_add(1, std::memory_order_release);

        // Debounced seat state drives the play-space recentre on both edges.
        const Halo3VehicleState state =
            unit == -1 ? Halo3VehicleState::Unknown
                       : (seated && anchorValid ? Halo3VehicleState::Vehicle : Halo3VehicleState::OnFoot);
        const Halo3VehicleState previousStable = g_odstVehicleFpDebounce.stable;
        if (g_odstVehicleFpDebounce.Update(state, kHalo3VehicleDebounceFrames))
        {
            g_odstVehicleFpStable.store(
                static_cast<uint32_t>(g_odstVehicleFpDebounce.stable), std::memory_order_relaxed);
            if (g_config.vehicle_first_person && (g_odstVehicleFpDebounce.stable == Halo3VehicleState::Vehicle ||
                                                     previousStable == Halo3VehicleState::Vehicle))
            {
                if (g_config.vehicle_recenter_on_seat)
                {
                    g_needPosRecenter.store(true, std::memory_order_release);
                    g_odstSeatRecenters.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
    }

    void OdstApplyHeadLook(void* source)
    {
        if (!source)
            return;
        float quaternion[4], headPosition[3];
        if (!VR_GetHeadPose(quaternion, headPosition))
            return;

        const float x = quaternion[0], y = quaternion[1];
        const float z = quaternion[2], w = quaternion[3];
        const float headForwardX = -2.0f * (w * y + x * z);
        const float headForwardY = 2.0f * (w * x - y * z);
        const float headForwardZ = -(1.0f - 2.0f * (x * x + y * y));
        const float headYaw = atan2f(headForwardX, -headForwardZ);
        const float headPitch = asinf(Clamp(headForwardY, -1.0f, 1.0f));

        const float headUpX = 2.0f * (x * y - w * z);
        const float headUpY = 1.0f - 2.0f * (x * x + z * z);
        const float headUpZ = 2.0f * (y * z + w * x);
        float horizonRightX = -headForwardZ;
        float horizonRightZ = headForwardX;
        float horizonLength = sqrtf(horizonRightX * horizonRightX + horizonRightZ * horizonRightZ);
        if (horizonLength < 1e-4f)
            horizonLength = 1e-4f;
        horizonRightX /= horizonLength;
        horizonRightZ /= horizonLength;
        const float neutralUpX = -headForwardY * horizonRightZ;
        const float neutralUpY = horizonLength;
        const float neutralUpZ = headForwardY * horizonRightX;
        const float headRoll = atan2f(headUpX * horizonRightX + headUpZ * horizonRightZ,
            headUpX * neutralUpX + headUpY * neutralUpY + headUpZ * neutralUpZ);

        const auto& layout = kOdstCameraProfile.layout;
        char* bytes = static_cast<char*>(source);
        float* forward = reinterpret_cast<float*>(bytes + layout.sourceForward);
        float* up = reinterpret_cast<float*>(bytes + layout.sourceUp);
        float* position = reinterpret_cast<float*>(bytes + layout.sourcePosition);

        const float stockForwardLength =
            sqrtf(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
        if (stockForwardLength < 1e-4f)
            return;
        const float stockYaw = atan2f(forward[1], forward[0]);

        // Halo 3 cutscene-facing parity: at each authored cinematic
        // cut, rebase the VR yaw reference so "forward" aligns with the new
        // shot's camera -- otherwise a cut can leave the viewer facing away from
        // the action. Between cuts the head looks around freely; pitch and roll
        // stay HMD-owned to avoid an artificial rotation during continuous
        // camera motion. Uses ODST's own resolved cinematic scene/shot state.
        static thread_local bool prevCinematic = false;
        static thread_local int32_t prevScene = -1;
        static thread_local int32_t prevShot = -1;
        int32_t cinematicScene = -1;
        int32_t cinematicShot = -1;
        const CinematicControlState cinematicControl = ReadOdstCinematicControl(cinematicScene, cinematicShot);
        const bool cinematic = cinematicControl == CinematicControlState::AuthoredLocked;
        // The firing hook runs on the engine's weapon path and must not read
        // cinematic TLS from that thread; publish it from here instead.
        g_odstCinematicLocked.store(cinematic ? 1u : 0u, std::memory_order_relaxed);
        const uint32_t cinematicGeneration = g_odstRuntimeGeneration.load(std::memory_order_relaxed);
        if (cinematicGeneration)
        {
            TitleAdapter_PublishCinematicControl(
                GameTitle::Halo3ODST, cinematicGeneration, cinematicControl, GetTickCount64());
            PublishCinematicSection(GameTitle::Halo3ODST, cinematic ? cinematicScene : -1); // -1 once it ends
        }
        if (cinematic && VR_IsCutsceneTheaterActive())
            return; // keep ODST's pristine authored camera on the theatre screen
        const bool cinematicBoundary =
            (cinematic && (!prevCinematic || cinematicScene != prevScene || cinematicShot != prevShot)) ||
            (!cinematic && prevCinematic);
        prevCinematic = cinematic;
        prevScene = cinematic ? cinematicScene : -1;
        prevShot = cinematic ? cinematicShot : -1;

        const bool manualRecenter = g_needRecenter.exchange(false, std::memory_order_acq_rel);
        if (manualRecenter || cinematicBoundary)
        {
            g_gameYawRef = stockYaw;
            g_headYawRef = headYaw;
            NoteYawReferenceLatched(GameTitle::Halo3ODST, cinematicGeneration, manualRecenter ? "recenter" : "cut",
                g_gameYawRef, g_headYawRef, true);
            if (manualRecenter)
            {
                memcpy(g_headPosRef, headPosition, sizeof(g_headPosRef));
                VR_NotePositionRecenter(); // the cabin fade re-captures its seated eye
                g_needPosRecenter.store(false, std::memory_order_release);
            }
            if (cinematicBoundary)
            {
                g_cinematicRebaseScene.store(cinematic ? cinematicScene : -1, std::memory_order_relaxed);
                g_cinematicRebaseShot.store(cinematic ? cinematicShot : -1, std::memory_order_relaxed);
                g_cinematicRebaseSerial.fetch_add(1, std::memory_order_release);
            }
        }
        else if (g_needPosRecenter.exchange(false, std::memory_order_acq_rel))
        {
            memcpy(g_headPosRef, headPosition, sizeof(g_headPosRef));
            VR_NotePositionRecenter(); // the cabin fade re-captures its seated eye
        }
        NoteSharedYawFrame(GameTitle::Halo3ODST, cinematicGeneration); // after the latch decision

        // Halo 3 parity: recentered yaw plus HMD-relative yaw; absolute HMD
        // pitch and roll. ODST's changing stock pitch/roll must not move the
        // tracked view through right-stick input, recoil, or authored shake.
        OdstHalo3LookAngles look{};
        if (!ComputeOdstHalo3LookAngles(g_gameYawRef, g_headYawRef, headYaw, headPitch, headRoll,
                g_yawSign.load(std::memory_order_relaxed), g_pitchSign.load(std::memory_order_relaxed),
                g_pitchTrim.load(std::memory_order_relaxed), look))
            return;
        const float gameYaw = look.yaw;
        {
            // The view yaw on the first raised-scope frame of each mission, so a
            // per-run picture shift can be checked against the yaw actually rendered.
            static uint32_t raisedLoggedGeneration = 0;
            const uint32_t generation = g_odstRuntimeGeneration.load(std::memory_order_relaxed);
            if (generation && raisedLoggedGeneration != generation && VR_IsScopeActive())
            {
                raisedLoggedGeneration = generation;
                LOG("ODST first raised scope frame: view yaw %.3f deg (game yaw reference %.3f, head yaw %.3f vs reference %.3f)",
                    gameYaw * 57.2957795f, g_gameYawRef * 57.2957795f, headYaw * 57.2957795f,
                    g_headYawRef * 57.2957795f);
            }
        }
        const float gamePitch = look.pitch;
        const float gameRoll = look.roll;
        const float cosPitch = cosf(gamePitch), sinPitch = sinf(gamePitch);
        const float cosYaw = cosf(gameYaw), sinYaw = sinf(gameYaw);
        // Optional vehicle-frame view. ON composes the same roll-free
        // hull frame Halo 3 uses -- yaw and pitch followed, roll deliberately
        // never -- so suspension bank or a side impact cannot tip the
        // player's horizon. OFF, stale, or non-finite falls straight through
        // to the established world-locked mapping below.
        float hullYaw = 0.0f, hullPitch = 0.0f;
        bool rotationFollows = false;
        if (Halo3ReadRollStableFollow(hullYaw, hullPitch))
        {
            const float trackedYawDelta =
                g_yawSign.load(std::memory_order_relaxed) * Halo3WrapPi(headYaw - g_headYawRef);
            float followBasis[9];
            if (Halo3ComposeFollowBasisWithRoll(
                    hullYaw, hullPitch, g_gameYawRef, trackedYawDelta, gamePitch, gameRoll, followBasis))
            {
                memcpy(forward, followBasis, sizeof(float) * 3);
                memcpy(up, followBasis + 6, sizeof(float) * 3);
                rotationFollows = true;
            }
        }
        if (!rotationFollows)
        {
            forward[0] = cosPitch * cosYaw;
            forward[1] = cosPitch * sinYaw;
            forward[2] = sinPitch;
            const float cosRoll = cosf(gameRoll), sinRoll = sinf(gameRoll);
            up[0] = (-sinPitch * cosYaw) * cosRoll + sinYaw * sinRoll;
            up[1] = (-sinPitch * sinYaw) * cosRoll - cosYaw * sinRoll;
            up[2] = cosPitch * cosRoll;
        }

        // Seat placement, in the same slot Halo 3 uses: after the rotation
        // is written and BEFORE the room-scale lean is added, so leaning still
        // works from the seat. The authored point already carries the trims,
        // the live rendered node and the occupant bounce, so consuming it here
        // is a plain overwrite. A cutscene keeps the authored camera.
        if (!cinematic && OdstVehicleFpActive())
        {
            float anchor[3];
            if (OdstComputeAuthoredAnchor(anchor))
                memcpy(position, anchor, sizeof(anchor));
        }

        OdstApplyPhysicalCrouchCamera(
            position, -Clamp((headPosition[1] - g_headPosRef[1]) * kOdstWorldUnitsPerMeter, -1.5f, 1.5f), !cinematic);
        Roomscale_Camera(GameTitle::Halo3ODST, !cinematic && Game_RoomscaleCameraAllowed(GameTitle::Halo3ODST),
            position, headPosition, quaternion, forward, g_headPosRef, kOdstWorldUnitsPerMeter);
        if (g_positional.load(std::memory_order_relaxed))
        {
            const float dx = headPosition[0] - g_headPosRef[0];
            const float dy = headPosition[1] - g_headPosRef[1];
            const float dz = headPosition[2] - g_headPosRef[2];
            float horizontalLength = sqrtf(headForwardX * headForwardX + headForwardZ * headForwardZ);
            if (horizontalLength < 1e-4f)
                horizontalLength = 1e-4f;
            const float horizontalForwardX = headForwardX / horizontalLength;
            const float horizontalForwardZ = headForwardZ / horizontalLength;
            const float forwardMove = dx * horizontalForwardX + dz * horizontalForwardZ;
            const float rightMove = dx * (-horizontalForwardZ) + dz * horizontalForwardX;
            const float scale = kOdstWorldUnitsPerMeter;
            // While the view follows the hull, leaning must move through
            // the SAME pitched frame or a lean would slide along the world
            // floor instead of the vehicle's. Tracked pitch and roll are
            // deliberately zero here: the orientation above already applied
            // them, and applying them twice double-pitches the lean.
            bool leanFollowed = false;
            if (rotationFollows)
            {
                const float trackedYawDelta =
                    g_yawSign.load(std::memory_order_relaxed) * Halo3WrapPi(headYaw - g_headYawRef);
                float positionBasis[9];
                float positionWorld[3];
                const float roomLocal[3] = {forwardMove, -rightMove, dy};
                if (Halo3ComposeFollowBasisWithRoll(
                        hullYaw, hullPitch, g_gameYawRef, trackedYawDelta, 0.0f, 0.0f, positionBasis) &&
                    Halo3TransformBasisVector(positionBasis, roomLocal, positionWorld))
                {
                    position[0] += Clamp(positionWorld[0] * scale, -1.5f, 1.5f);
                    position[1] += Clamp(positionWorld[1] * scale, -1.5f, 1.5f);
                    position[2] += Clamp(positionWorld[2] * scale, -1.5f, 1.5f);
                    leanFollowed = true;
                }
            }
            if (!leanFollowed)
            {
                position[0] += Clamp((cosYaw * forwardMove + sinYaw * rightMove) * scale, -1.5f, 1.5f);
                position[1] += Clamp((sinYaw * forwardMove - cosYaw * rightMove) * scale, -1.5f, 1.5f);
                position[2] += Clamp(dy * scale, -1.5f, 1.5f);
            }
        }
    }

    // ODST vehicle evidence probe. Read-only and inert - it publishes
    // nothing any camera, HUD or input path consumes; its entire product is
    // log lines that prove (or disprove) the vehicle-detection offsets against a live game.
    //
    // Deliberately cheap and allocation-free: one pass every two seconds, and
    // it only logs when the observed state actually changes, so an ordinary
    // session produces a handful of lines rather than a flood.
    inline constexpr uint64_t kOdstProbeIntervalMs = 2000;

    // Walks every LIVE object in the engine's object table once per level and
    // reports the distinct vehicle definitions it finds. The table is the
    // engine's generic data array; its header carries a name and a 'd@t@'
    // signature, and this refuses to walk anything that does not match, so a
    // wrong TLS slot can only produce a loud log line, never a bad read.
    void OdstVehicleCensus(uint64_t nowMs)
    {
        (void)nowMs;
        if (g_odstNodeBinding.load(std::memory_order_acquire) !=
            static_cast<uint8_t>(OdstVehicleBindingState::Installed))
        {
            // The census reports node banks; without the node binding there is
            // nothing extra to learn beyond what the seated probe already logs.
            return;
        }
        // Once per runtime generation is enough: definitions do not change
        // within a level, and this walk is the most expensive thing here.
        static uint32_t censusGeneration = 0;
        const uint32_t generation = g_odstRuntimeGeneration.load(std::memory_order_acquire);
        if (!generation || generation == censusGeneration)
            return;

        struct Seen
        {
            uint32_t definition;
            int physicsType;
            int nodeBankSize;
            int tagNodes;
            uint32_t seat0Flags;
            uint32_t seatCount;
            bool defOk;
        };
        Seen seen[16]{};
        int seenCount = 0;
        int liveObjects = 0;
        int vehicles = 0;
        uint32_t firstUnallocated = 0;
        bool headerOk = false;
        bool faulted = false;
        __try
        {
            auto** slots = reinterpret_cast<void**>(__readgsqword(0x58));
            auto* tls = slots ? reinterpret_cast<unsigned char*>(slots[*g_odstEngineTlsIndex]) : nullptr;
            unsigned char* table = tls ? *reinterpret_cast<unsigned char**>(tls + kOdstTlsObjectTableOffset) : nullptr;
            if (table)
            {
                OdstDataArrayHeaderView header{};
                header.nameIsObject = memcmp(table + kOdstDataArrayNameOffset, "object", 7) == 0;
                header.signature = *reinterpret_cast<const uint32_t*>(table + kOdstDataArraySignatureOffset);
                header.maximumCount = *reinterpret_cast<const uint32_t*>(table + kOdstDataArrayMaxCountOffset);
                header.elementSize = *reinterpret_cast<const uint32_t*>(table + kOdstDataArrayElementSizeOffset);
                header.firstUnallocated =
                    *reinterpret_cast<const uint32_t*>(table + kOdstDataArrayFirstUnallocatedOffset);
                header.valid = *(table + kOdstDataArrayValidOffset);
                headerOk = OdstObjectTableIsWalkable(header);
                firstUnallocated = header.firstUnallocated;
                unsigned char* entries =
                    headerOk ? *reinterpret_cast<unsigned char**>(table + kOdstDataArrayElementsOffset) : nullptr;
                if (entries)
                {
                    censusGeneration = generation;
                    for (uint32_t i = 0; i < firstUnallocated; ++i)
                    {
                        unsigned char* entry = entries + static_cast<size_t>(i) * kOdstObjectEntryStride;
                        const uint16_t identifier =
                            *reinterpret_cast<const uint16_t*>(entry + kOdstObjectEntryIdentifierOffset);
                        if (!OdstObjectEntryIsLive(identifier))
                            continue; // a hole below the high-water mark
                        ++liveObjects;
                        if (*(entry + kOdstObjectEntryKindOffset) != kOdstObjectKindVehicle)
                            continue;
                        unsigned char* data = *reinterpret_cast<unsigned char**>(entry + kOdstObjectEntryDataOffset);
                        if (!data)
                            continue;
                        ++vehicles;
                        const uint16_t definitionIndex = *reinterpret_cast<const uint16_t*>(data);
                        bool known = false;
                        for (int s = 0; s < seenCount; ++s)
                            known |= seen[s].definition == definitionIndex;
                        if (known || seenCount >= 16)
                            continue;
                        Seen& record = seen[seenCount++];
                        record.definition = definitionIndex;
                        record.physicsType = g_odstVehicleTypeAccessor(definitionIndex);
                        record.nodeBankSize = *reinterpret_cast<const int16_t*>(data + kOdstObjectNodeBankSizeOffset);
                        record.tagNodes = -1;
                        unsigned char* definition = OdstLoadedTagDefinition(definitionIndex);
                        if (!definition)
                            continue;
                        record.seatCount =
                            *reinterpret_cast<const uint32_t*>(definition + kOdstVehicleSeatsCountOffset);
                        const uint32_t seatsAddress =
                            *reinterpret_cast<const uint32_t*>(definition + kOdstVehicleSeatsDataOffset);
                        void** baseSlot = g_odstTagDataBase;
                        auto* tagBase = baseSlot ? static_cast<unsigned char*>(*baseSlot) : nullptr;
                        if (tagBase && seatsAddress && record.seatCount && record.seatCount <= 64)
                        {
                            unsigned char* seats = tagBase + static_cast<size_t>(seatsAddress) * 4;
                            record.seat0Flags = *reinterpret_cast<const uint32_t*>(seats + kOdstSeatFlagsOffset);
                            record.defOk = true;
                        }
                        const uint32_t modelDatum =
                            *reinterpret_cast<const uint32_t*>(definition + kOdstVehicleModelRefOffset);
                        unsigned char* model = OdstLoadedTagDefinition(modelDatum);
                        const uint32_t renderDatum =
                            model ? *reinterpret_cast<const uint32_t*>(model + kOdstModelRenderRefOffset) : 0xFFFFFFFFu;
                        unsigned char* renderModel = OdstLoadedTagDefinition(renderDatum);
                        if (renderModel)
                        {
                            record.tagNodes =
                                *reinterpret_cast<const int32_t*>(renderModel + kOdstRenderModelNodesCountOffset);
                        }
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("the ODST vehicle census faulted");
            faulted = true;
        }

        if (faulted)
        {
            g_odstVehicleProbeFaults.fetch_add(1, std::memory_order_relaxed);
            LOG("ODST vehProbe census: FAULTED walking the object table; the "
                "table layout under test is wrong for this build");
            return;
        }
        if (!headerOk)
        {
            LOG("ODST vehProbe census: the object table failed its own "
                "name/'d@t@' signature proof, so nothing was walked. The TLS "
                "slot +0x%X does not reach this build's object table",
                (unsigned)kOdstTlsObjectTableOffset);
            return;
        }
        LOG("ODST vehProbe census: %d live objects (high water %u), %d "
            "vehicles, %d distinct definitions",
            liveObjects, firstUnallocated, vehicles, seenCount);
        for (int s = 0; s < seenCount; ++s)
        {
            const Seen& record = seen[s];
            OdstProbeVehicleRecord bank{};
            bank.nodeBankByteSize = record.nodeBankSize;
            bank.tagNodeCount = record.tagNodes;
            OdstDefinitionFields fields{};
            fields.physicsType = record.physicsType;
            fields.seatFlagsValid = record.defOk;
            fields.seat0Flags = record.seat0Flags;
            // A census entry is an unoccupied or AI-crewed object, so the
            // engine's own in-vehicle answer is not available here; the shade
            // and turret both land on Unknown until a seated sample confirms.
            fields.inVehicle = false;
            LOG("ODST vehProbe census: def=0x%X type=%d seats=%u "
                "seat0Flags=0x%X (driver=%d gunner=%d invalidForPlayer=%d) "
                "nodeBank=%d tagNodes=%d coherent=%d provisionalId=%s",
                (unsigned)record.definition, record.physicsType, record.seatCount, record.seat0Flags,
                (record.seat0Flags & kOdstSeatDriverBit) ? 1 : 0, (record.seat0Flags & kOdstSeatGunnerBit) ? 1 : 0,
                (record.seat0Flags & kOdstSeatInvalidForPlayerBit) ? 1 : 0, record.nodeBankSize, record.tagNodes,
                OdstNodeBankIsCoherent(bank) ? 1 : 0, OdstVehicleIdName(OdstResolveVehicleId(fields)));
        }
    }

    void OdstVehicleProbe(uint64_t nowMs)
    {
        if (g_odstVehicleBinding.load(std::memory_order_acquire) !=
                static_cast<uint8_t>(OdstVehicleBindingState::Installed) ||
            !g_odstEngineTlsIndex)
            return;
        const uint64_t last = g_odstVehicleProbeMs.load(std::memory_order_relaxed);
        if (nowMs - last < kOdstProbeIntervalMs)
            return;
        g_odstVehicleProbeMs.store(nowMs, std::memory_order_relaxed);

        // Everything below is observation only. A fault here must never reach
        // the camera copy that called us, so the whole walk is guarded and the
        // fault count is logged rather than retried.
        int unit = -1;
        int seat = -1;
        int parentHandle = -1;
        unsigned parentKind = 0;
        unsigned native = 0;
        bool tlsOk = false;
        bool entriesOk = false;
        uint32_t defIndex = 0xFFFFFFFFu;
        int physicsType = -1;
        int nodeBankSize = -1;
        int nodeBankRel = 0;
        int tagNodeCount = -1;
        uint32_t seat0Flags = 0;
        uint32_t seatCount = 0;
        bool defOk = false;
        bool faulted = false;
        __try
        {
            auto** slots = reinterpret_cast<void**>(__readgsqword(0x58));
            auto* tls = slots ? reinterpret_cast<unsigned char*>(slots[*g_odstEngineTlsIndex]) : nullptr;
            unsigned char* table = tls ? *reinterpret_cast<unsigned char**>(tls + kOdstTlsObjectTableOffset) : nullptr;
            tlsOk = table != nullptr;
            unsigned char* entries =
                table ? *reinterpret_cast<unsigned char**>(table + kOdstObjectTableEntriesOffset) : nullptr;
            entriesOk = entries != nullptr;

            unit = g_odstPlayerUnitGetter(0);
            // The firing hook compares this against the unit the engine hands
            // it, so somebody else's weapon can never take our seat's origin.
            g_odstSeatedPlayerUnit.store(unit, std::memory_order_relaxed);
            if (unit != -1 && entries)
            {
                unsigned char* data = *reinterpret_cast<unsigned char**>(
                    entries + static_cast<size_t>(unit & 0xFFFF) * kOdstObjectEntryStride + kOdstObjectEntryDataOffset);
                if (data)
                {
                    // The negative control: on foot this must read exactly -1,
                    // every pass. A wrong offset would not hold that.
                    seat = *reinterpret_cast<const int16_t*>(data + kOdstUnitSeatWordOffset);
                    native = g_odstUnitInVehicle(unit);
                    parentHandle = *reinterpret_cast<const int32_t*>(data + kOdstObjectParentOffset);
                    if (!OdstSeatWordMeansUnseated(seat) && parentHandle != -1)
                    {
                        unsigned char* parentEntry =
                            entries + static_cast<size_t>(parentHandle & 0xFFFF) * kOdstObjectEntryStride;
                        parentKind = *(parentEntry + kOdstObjectEntryKindOffset);
                        unsigned char* parentData =
                            *reinterpret_cast<unsigned char**>(parentEntry + kOdstObjectEntryDataOffset);
                        if (parentData && parentKind == kOdstObjectKindVehicle)
                        {
                            const uint16_t definitionIndex = *reinterpret_cast<const uint16_t*>(parentData);
                            defIndex = definitionIndex;
                            physicsType = g_odstVehicleTypeAccessor(definitionIndex);
                            nodeBankSize =
                                *reinterpret_cast<const int16_t*>(parentData + kOdstObjectNodeBankSizeOffset);
                            nodeBankRel = *reinterpret_cast<const int16_t*>(parentData + kOdstObjectNodeBankRelOffset);
                            unsigned char* definition = OdstLoadedTagDefinition(definitionIndex);
                            if (definition)
                            {
                                seatCount =
                                    *reinterpret_cast<const uint32_t*>(definition + kOdstVehicleSeatsCountOffset);
                                const uint32_t seatsAddress =
                                    *reinterpret_cast<const uint32_t*>(definition + kOdstVehicleSeatsDataOffset);
                                void** baseSlot = g_odstTagDataBase;
                                auto* tagBase = baseSlot ? static_cast<unsigned char*>(*baseSlot) : nullptr;
                                if (tagBase && seatsAddress && seatCount && seatCount <= 64)
                                {
                                    unsigned char* seats = tagBase + static_cast<size_t>(seatsAddress) * 4;
                                    seat0Flags = *reinterpret_cast<const uint32_t*>(seats + kOdstSeatFlagsOffset);
                                    defOk = true;
                                }
                                // Render-model node count, for the node-bank
                                // coherence proof (matrices must equal nodes).
                                // Datum layout is the same as Halo 3's: the
                                // tag reference's datum sits at the block
                                // offset itself.
                                const uint32_t modelDatum =
                                    *reinterpret_cast<const uint32_t*>(definition + kOdstVehicleModelRefOffset);
                                unsigned char* model = OdstLoadedTagDefinition(modelDatum);
                                const uint32_t renderDatum =
                                    model ? *reinterpret_cast<const uint32_t*>(model + kOdstModelRenderRefOffset)
                                          : 0xFFFFFFFFu;
                                unsigned char* renderModel = OdstLoadedTagDefinition(renderDatum);
                                if (renderModel)
                                {
                                    tagNodeCount = *reinterpret_cast<const int32_t*>(
                                        renderModel + kOdstRenderModelNodesCountOffset);
                                }
                            }
                        }
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("the ODST vehicle probe faulted");
            faulted = true;
        }

        if (faulted)
        {
            const uint32_t faults = g_odstVehicleProbeFaults.fetch_add(1, std::memory_order_relaxed) + 1;
            if (faults <= 3)
            {
                LOG("ODST vehProbe: FAULTED while reading engine state "
                    "(fault %u); the offsets under test are wrong for this "
                    "build and nothing may be built on them",
                    faults);
            }
            return;
        }

        // The census below is what makes this candidate answerable without the
        // player entering anything: every AI-crewed and parked vehicle in the
        // level exercises the same definition, physics-type and node-bank
        // reads the real sampler will use.
        OdstVehicleCensus(nowMs);

        // Log only on a change of the observed shape, so a whole mission emits
        // a readable handful of lines.
        OdstProbeVehicleRecord record{};
        record.dataValid = defOk;
        record.definitionIndex = defIndex;
        record.physicsType = physicsType;
        record.nodeBankByteSize = nodeBankSize;
        record.nodeBankRelOffset = nodeBankRel;
        record.tagNodeCount = tagNodeCount;

        OdstDefinitionFields fields{};
        fields.physicsType = physicsType;
        fields.seatFlagsValid = defOk;
        fields.seat0Flags = seat0Flags;
        fields.inVehicle = native != 0;
        const OdstVehicleId id = OdstResolveVehicleId(fields);

        static uint64_t loggedKey = ~0ull;
        const uint64_t key = (static_cast<uint64_t>(defIndex & 0xFFFFu) << 48) ^
                             (static_cast<uint64_t>(static_cast<uint16_t>(seat + 1)) << 32) ^
                             (static_cast<uint64_t>(physicsType & 0xFF) << 24) ^
                             (static_cast<uint64_t>(native & 1) << 16) ^ (static_cast<uint64_t>(tlsOk ? 1 : 0) << 8) ^
                             static_cast<uint64_t>(entriesOk ? 1 : 0);
        if (key == loggedKey)
            return;
        loggedKey = key;

        if (OdstSeatWordMeansUnseated(seat))
        {
            LOG("ODST vehProbe: on foot - tls chain %s, entries %s, "
                "unit=0x%X seat=%d (expected -1) native=%u. This is the "
                "negative control for the seat word at unit+0x%X",
                tlsOk ? "OK" : "NULL", entriesOk ? "OK" : "NULL", (unsigned)unit, seat, native,
                (unsigned)kOdstUnitSeatWordOffset);
            return;
        }

        LOG("ODST vehProbe: seated - seat=%d native=%u parent=0x%X kind=%u "
            "def=0x%X type=%d id=%s seats=%u seat0Flags=0x%X "
            "(driver=%d gunner=%d thirdPerson=%d invalidForPlayer=%d) "
            "nodeBank size=%d rel=%d tagNodes=%d coherent=%d",
            seat, native, (unsigned)parentHandle, parentKind, (unsigned)defIndex, physicsType, OdstVehicleIdName(id),
            seatCount, seat0Flags, (seat0Flags & kOdstSeatDriverBit) ? 1 : 0, (seat0Flags & kOdstSeatGunnerBit) ? 1 : 0,
            (seat0Flags & kOdstSeatThirdPersonCameraBit) ? 1 : 0, (seat0Flags & kOdstSeatInvalidForPlayerBit) ? 1 : 0,
            nodeBankSize, nodeBankRel, tagNodeCount, OdstNodeBankIsCoherent(record) ? 1 : 0);
    }

    __declspec(noinline) void* __fastcall OdstCamCopyBody(void* destination, void* source)
    {
        CamCopyFn original = g_odstCamera.originalCamCopy;
        if (!original)
            return destination;
        const auto& layout = kOdstCameraProfile.layout;
        const uintptr_t primaryDestination =
            g_odstCamera.gunCameraArray ? g_odstCamera.gunCameraArray + layout.rootCurrentCompact : 0;
        const bool ownsPrimaryCamera =
            primaryDestination && reinterpret_cast<uintptr_t>(destination) == primaryDestination;
        const bool singleUserPath =
            ownsPrimaryCamera && OdstSingleUserTailIsValid(reinterpret_cast<const void*>(g_odstCamera.gunCameraArray));
        // Any active camera in our proven slot-0 view keeps the core alive and
        // receives Halo 3's camera ownership. Halo 3 publishes the pre-head-look
        // aim forward on EVERY live camera copy; it does not gate vehicle aim on
        // a first-person blend. ODST's settled vehicle camera is blend 0, so the
        // old blend gate disabled the right-controller path for the entire ride.
        const bool ownsActiveCamera = g_odstCamera.installed.load(std::memory_order_acquire) && singleUserPath &&
                                      OdstSourceCameraIsActive(source);
        // Halo 3 applies headset ownership to every active observer camera,
        // including vehicles, death, and cinematics. Do the same for ODST: a
        // blend-0 active camera still owns both head look and continuous aim.
        const bool transform = ownsActiveCamera && g_odstCamera.armed.load(std::memory_order_acquire) &&
                               !g_odstCamera.teardownRequested.load(std::memory_order_acquire) &&
                               g_enabled.load(std::memory_order_relaxed);
        float savedPosition[3]{}, savedForward[3]{}, savedUp[3]{};
        if (ownsActiveCamera)
        {
            // Sample the seat before anything reads it this frame, exactly
            // where Halo 3 samples in its own camera copy. OdstApplyHeadLook
            // below consumes the anchor it publishes.
            OdstSampleVehicleState(GetTickCount64());
            // Integrate the hull heading and publish its roll-free pitch
            // before the head look and the VR turn read either.
            OdstApplySeatYawFollow();
            // Engaged on SEAT OCCUPANCY, deliberately not on the per-frame
            // liveness predicate the camera uses. OdstVehicleFpActive() also
            // requires a sample fresh within 500 ms; a momentary stale sample
            // is a reason to stop steering the camera, but handing auto
            // exposure back for those frames is exactly what let the dash keep
            // driving the brightness. Occupancy is already debounced.
            const bool seatOwnsView = g_config.vehicle_first_person &&
                                      g_odstVehicleBinding.load(std::memory_order_acquire) ==
                                          static_cast<uint8_t>(OdstVehicleBindingState::Installed) &&
                                      g_odstVehicleFpStable.load(std::memory_order_relaxed) ==
                                          static_cast<uint32_t>(Halo3VehicleState::Vehicle);
            OdstApplyExposureLock(g_config.vehicle_steady_exposure && seatOwnsView);
            ApplyOdstMotionBlurSetting();
            // Exact Halo 3 control ownership: publish the source camera's
            // pre-head-look forward for every active camera copy. Vehicles
            // then consume the same continuous closed-loop right-stick aim
            // that guides Halo 3 vehicles. Reads only the proven source
            // layout; no new offset or vehicle-specific patch.
            const char* srcBytes = static_cast<const char*>(source);
            float aimForward[3];
            memcpy(aimForward, srcBytes + layout.sourceForward, sizeof(aimForward));
            if (isfinite(aimForward[0]) && isfinite(aimForward[1]) && isfinite(aimForward[2]))
            {
                g_aimFwdX.store(aimForward[0]);
                g_aimFwdY.store(aimForward[1]);
                g_aimFwdZ.store(aimForward[2]);
                g_aimSeen = true;
            }
        }
        if (transform)
        {
            ApplyVrTurn();
            const char* bytes = static_cast<const char*>(source);
            memcpy(savedPosition, bytes + layout.sourcePosition, sizeof(savedPosition));
            memcpy(savedForward, bytes + layout.sourceForward, sizeof(savedForward));
            memcpy(savedUp, bytes + layout.sourceUp, sizeof(savedUp));
            g_baseCamX.store(savedPosition[0]);
            g_baseCamY.store(savedPosition[1]);
            g_baseCamZ.store(savedPosition[2]);
            g_baseCamValid.store(true, std::memory_order_release);
            OdstApplyHeadLook(source);
            const char* transformedBytes = static_cast<const char*>(source);
            const float* transformedPosition = reinterpret_cast<const float*>(transformedBytes + layout.sourcePosition);
            const float* transformedForward = reinterpret_cast<const float*>(transformedBytes + layout.sourceForward);
            const float* transformedUp = reinterpret_cast<const float*>(transformedBytes + layout.sourceUp);
            g_camX.store(transformedPosition[0]);
            g_camY.store(transformedPosition[1]);
            g_camZ.store(transformedPosition[2]);
            for (int axis = 0; axis < 3; ++axis)
            {
                g_camFwd[axis].store(transformedForward[axis]);
                g_camUp[axis].store(transformedUp[axis]);
            }
            g_camValid.store(true, std::memory_order_release);
            if (!g_worldUpInit.load(std::memory_order_acquire))
            {
                const float length = sqrtf(transformedUp[0] * transformedUp[0] + transformedUp[1] * transformedUp[1] +
                                           transformedUp[2] * transformedUp[2]);
                if (length > 1e-4f)
                {
                    for (int axis = 0; axis < 3; ++axis)
                        g_worldUp[axis].store(transformedUp[axis] / length, std::memory_order_relaxed);
                    g_worldUpInit.store(true, std::memory_order_release);
                }
            }
            else
            {
                float worldUp[3] = {g_worldUp[0].load(std::memory_order_relaxed),
                    g_worldUp[1].load(std::memory_order_relaxed), g_worldUp[2].load(std::memory_order_relaxed)};
                const float lookLevel = fabsf(transformedForward[0] * worldUp[0] + transformedForward[1] * worldUp[1] +
                                              transformedForward[2] * worldUp[2]);
                if (lookLevel < 0.42f)
                {
                    constexpr float kBlend = 0.01f;
                    for (int axis = 0; axis < 3; ++axis)
                        worldUp[axis] += (transformedUp[axis] - worldUp[axis]) * kBlend;
                    const float length =
                        sqrtf(worldUp[0] * worldUp[0] + worldUp[1] * worldUp[1] + worldUp[2] * worldUp[2]);
                    if (length > 1e-4f)
                        for (int axis = 0; axis < 3; ++axis)
                            g_worldUp[axis].store(worldUp[axis] / length, std::memory_order_relaxed);
                }
            }
        }
        else if (ownsActiveCamera)
        {
            g_camValid.store(false, std::memory_order_release);
            g_baseCamValid.store(false, std::memory_order_release);
        }
        else if (OdstCamCopyRequestsTeardown(
                     g_odstCamera.armed.load(std::memory_order_acquire), ownsPrimaryCamera, singleUserPath))
        {
            // Our slot-0 view object no longer matches the single-user layout:
            // a genuine level unload/transition, not a mere non-FP camera. An
            // active third-person camera (singleUserPath still valid) is NOT a
            // teardown -- it renders stock and keeps the core armed.
            OdstRequestFallback(OdstSourceCameraIsActive(source) ? OdstFallbackReason::UnsupportedCameraMode
                                                                 : OdstFallbackReason::LevelUnloaded);
        }
        void* result = original(destination, source);
        if (transform)
        {
            char* bytes = static_cast<char*>(source);
            memcpy(bytes + layout.sourcePosition, savedPosition, sizeof(savedPosition));
            memcpy(bytes + layout.sourceForward, savedForward, sizeof(savedForward));
            memcpy(bytes + layout.sourceUp, savedUp, sizeof(savedUp));
        }
        if (ownsActiveCamera)
        {
            // Match Halo 3: every active camera copy is an aim/reticle timing
            // signal as well as a heartbeat, including the blend-0 vehicle.
            if (g_odstCamera.armed.load(std::memory_order_acquire))
                VR_NotifyCameraTransform();
            const uint64_t cameraNowMs = GetTickCount64();
            g_odstLastCamCopyMs.store(cameraNowMs, std::memory_order_release);
            const uint32_t runtimeGeneration = g_odstRuntimeGeneration.load(std::memory_order_acquire);
            if (runtimeGeneration)
            {
                TitleAdapter_PublishHeartbeat(GameTitle::Halo3ODST, runtimeGeneration, cameraNowMs);
                // Evidence probe: kept alongside the sampler so one session
                // produces both the offset proof and the seat behavior.
                OdstVehicleProbe(cameraNowMs);
            }
            g_odstCamera.sawValidCamera.store(true, std::memory_order_release);
        }
        return result;
    }

    __declspec(noinline) void __fastcall OdstObserverCameraEffectHook(int userIndex)
    {
        g_odstCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        ObserverCameraEffectFn original = g_odstCamera.originalObserverCameraEffect;
        const bool suppress = g_odstCamera.armed.load(std::memory_order_acquire) &&
                              !g_odstCamera.teardownRequested.load(std::memory_order_acquire) &&
                              g_enabled.load(std::memory_order_relaxed);
        if (!suppress && original)
            original(userIndex);
        g_odstCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }

    __declspec(noinline) void* __fastcall OdstCamCopyHook(void* destination, void* source)
    {
        // The complete wrapper is unwind-backed and scanned during teardown,
        // including its prologue before this increment and its epilogue after
        // the decrement. If a C++ exception ever escapes the body, the
        // decrement is deliberately skipped so cleanup stays fail-closed.
        g_odstCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        void* result = OdstCamCopyBody(destination, source);
        g_odstCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        return result;
    }

    __declspec(noinline) void __fastcall OdstFpCameraRebuildBody(void* view, unsigned char flag)
    {
        FpCameraRebuildFn original = g_odstCamera.originalFpCameraRebuild;
        if (!original)
            return;
        original(view, flag);
        char* eyeView = static_cast<char*>(g_odstCamera.eyeView.load(std::memory_order_acquire));
        if (!view || !g_odstCamera.armed.load(std::memory_order_acquire) || !eyeView)
            return;
        const auto& layout = kOdstCameraProfile.layout;
        if (view != eyeView + layout.nestedFpBase)
        {
            OdstRequestFallback(OdstFallbackReason::UnsupportedCameraMode);
            return;
        }
        char* bytes = static_cast<char*>(view);
        memcpy(bytes + layout.rootCurrentCompact, g_odstCamera.eyeCompactCamera, layout.compactSize);
        memcpy(bytes + layout.rootSecondaryDerived, g_odstCamera.eyeDerivedBlock, layout.derivedSize);
        if (g_odstCamera.fpCameraUpload)
            g_odstCamera.fpCameraUpload(bytes + layout.rootCurrentCompact, bytes + layout.rootSecondaryDerived);
    }

    __declspec(noinline) void __fastcall OdstFpCameraRebuildHook(void* view, unsigned char flag)
    {
        g_odstCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        OdstFpCameraRebuildBody(view, flag);
        g_odstCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }

    __declspec(noinline) void __fastcall OdstFpDriverBody(void* view, unsigned char flag)
    {
        FpDriverFn original = g_odstCamera.originalFpDriver;
        if (!original)
            return;
        void* eyeView = g_odstCamera.eyeView.load(std::memory_order_acquire);
        if (view && g_odstCamera.armed.load(std::memory_order_acquire) && eyeView)
        {
            const auto& layout = kOdstCameraProfile.layout;
            if (view != eyeView || !OdstSingleUserTailIsValid(view))
            {
                OdstRequestFallback(OdstFallbackReason::UnsupportedCameraMode);
                original(view, flag);
                return;
            }
            char* bytes = static_cast<char*>(view);
            memcpy(bytes + layout.rootSecondaryCompact, g_odstCamera.eyeCompactCamera, layout.compactSize);
            memcpy(bytes + layout.rootSecondaryDerived, g_odstCamera.eyeDerivedBlock, layout.derivedSize);
            memcpy(bytes + layout.nestedCurrentCompact, g_odstCamera.eyeCompactCamera, layout.compactSize);
            memcpy(bytes + layout.nestedSecondaryDerived, g_odstCamera.eyeDerivedBlock, layout.derivedSize);
            if (g_odstCamera.fpCameraUpload)
                g_odstCamera.fpCameraUpload(bytes + layout.nestedCurrentCompact, bytes + layout.nestedSecondaryDerived);
        }
        original(view, flag);
    }

    __declspec(noinline) void __fastcall OdstFpDriverHook(void* view, unsigned char flag)
    {
        g_odstCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        OdstFpDriverBody(view, flag);
        g_odstCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }

    // ODST weapon/arm adapter. The shared Halo 3 solver owns all player-facing
    // behavior and configuration; only this title-proven skeleton topology and
    // these ODST-resolved hook originals live in the adapter.
    constinit thread_local float g_odstFpRootScale[2] = {1.0f, 1.0f};

    bool BuildOdstCenterFpRoot(int slot, BoneMatrix& root)
    {
        if (slot < 0 || slot > 1 || !g_camValid.load())
            return false;
        float basis[9];
        if (!LoadCameraBasis(basis))
            return false;
        root = {};
        const float scale = g_odstFpRootScale[slot];
        root.scale = isfinite(scale) && fabsf(scale) > 0.001f ? scale : 1.0f;
        memcpy(root.rotation, basis, sizeof(basis));
        root.translation[0] = g_camX.load();
        root.translation[1] = g_camY.load();
        root.translation[2] = g_camZ.load();
        return true;
    }

    void MeasureOdstAuthoredBarrel(int slot, const BoneMatrix* bones, int wrist)
    {
        const float* wr = bones[wrist].rotation;
        float barrel[3] = {wr[0], wr[3], wr[6]};
        float length = sqrtf(barrel[0] * barrel[0] + barrel[1] * barrel[1] + barrel[2] * barrel[2]);
        if (length <= 1e-4f || !isfinite(length))
            return;
        for (float& component : barrel)
            component /= length;
        if (g_barrelInWristValid[slot].load(std::memory_order_relaxed))
        {
            constexpr float kBlend = 0.02f;
            for (int axis = 0; axis < 3; ++axis)
                barrel[axis] = g_barrelInWrist[slot][axis].load(std::memory_order_relaxed) * (1.0f - kBlend) +
                               barrel[axis] * kBlend;
            length = sqrtf(barrel[0] * barrel[0] + barrel[1] * barrel[1] + barrel[2] * barrel[2]);
            if (length > 1e-4f)
                for (float& component : barrel)
                    component /= length;
        }
        for (int axis = 0; axis < 3; ++axis)
            g_barrelInWrist[slot][axis].store(barrel[axis], std::memory_order_relaxed);
        g_barrelInWristValid[slot].store(true, std::memory_order_release);
    }

    // ---- One-shot FP weapon-layout self-check (headset diagnostics) ---------
    // The two remaining fail-closed risks for the ODST weapon/arm path are (1)
    // the FP interpolation hook never firing on a weapon slot and (2) the live
    // node count falling outside ComputeOdstFpSkeletonLayout's accepted 39..64
    // range (H3ODSTEK-derived indices vs. the retail skeleton). Neither can be
    // logged from this hot hook, so it publishes atomically and the 50 ms worker
    // emits one line -- exactly the pattern the non-FP camera capture uses below.
    struct OdstFpLayoutSelfCheck
    {
        std::atomic<uint32_t> key{0}; // 0 = nothing observed yet
        std::atomic<int> slot{-1};
        std::atomic<int> nodeCount{-1};
        std::atomic<int> accepted{0}; // 1 = layout accepted the node count
        std::atomic<int> wrist{-1};
        std::atomic<int> cameraControl{-1};
    };
    constinit OdstFpLayoutSelfCheck g_odstFpLayoutSelfCheck;
    constinit std::atomic<uint32_t> g_odstFpLayoutLoggedKey{0};

    // Atomic-only publish from the hot FP interpolation hook. Deduped by a key
    // built from slot/count/accepted so it republishes only when the observation
    // changes; the worker then emits each distinct observation at most once.
    void PublishOdstFpLayoutSelfCheck(int slot, int nodeCount, bool accepted, const OdstFpSkeletonLayout& layout)
    {
        const uint32_t key =
            (static_cast<uint32_t>(slot & 0x3) << 30) | (static_cast<uint32_t>(accepted ? 1u : 0u) << 29) |
            (static_cast<uint32_t>(nodeCount & 0x1FF) << 20) | 0x1u; // low bit set so a real observation is never key 0
        OdstFpLayoutSelfCheck& sc = g_odstFpLayoutSelfCheck;
        if (sc.key.load(std::memory_order_relaxed) == key)
            return;
        sc.slot.store(slot, std::memory_order_relaxed);
        sc.nodeCount.store(nodeCount, std::memory_order_relaxed);
        sc.accepted.store(accepted ? 1 : 0, std::memory_order_relaxed);
        sc.wrist.store(accepted ? layout.rightWrist : -1, std::memory_order_relaxed);
        sc.cameraControl.store(accepted ? layout.cameraControl : -1, std::memory_order_relaxed);
        sc.key.store(key, std::memory_order_release);
    }

    // ---- Weapon-order diagnostic tranche (read-only evidence) ----
    void PublishOdstFpDiagnostic(WeaponOrderEventKind kind, WeaponOrderEventStatus status, uint64_t preparedSerial,
        uint32_t unit, uint32_t weapon, uint64_t aux0, uint64_t aux1) noexcept
    {
        if (!Telemetry_WeaponEventsAccepting())
            return;
        Telemetry_PublishWeaponEvent(static_cast<uint8_t>(kind), static_cast<uint8_t>(status),
            static_cast<uint8_t>(GameTitle::Halo3ODST), g_odstRuntimeGeneration.load(std::memory_order_acquire),
            preparedSerial, unit, weapon, aux0, aux1);
    }

    bool OdstFpInterpolateWeaponBody(int view, int id, int slot, BoneMatrix** outBones, int* outCount)
    {
        // Diagnostic-only FP entry + ungated before-read (no gameplay effect).
        uint32_t diagnosticBeforeUnit = UINT32_MAX;
        uint32_t diagnosticBeforeWeapon = UINT32_MAX;
        uint32_t diagnosticBeforeDetail = 0;
        const bool diagnosticArmed = Telemetry_WeaponEventsAccepting() && slot == 0 && view == 0;
        if (diagnosticArmed)
        {
            Game_DiagnosticNoteFpThread(GameTitle::Halo3ODST);
            PublishOdstFpDiagnostic(WeaponOrderEventKind::FpEntry, WeaponOrderEventStatus::NoObservation,
                g_fpStereoSolveScope.armed ? g_fpStereoSolveScope.anatomicalTracking.serial : 0, UINT32_MAX, UINT32_MAX,
                uint64_t(slot), uint64_t(view));
            uint32_t probeUnit = UINT32_MAX, probeWeapon = UINT32_MAX;
            uint32_t probeDetail = 0;
            if (Game_DiagnosticReadPrimaryWeapon(GameTitle::Halo3ODST, probeUnit, probeWeapon, probeDetail))
            {
                diagnosticBeforeUnit = probeUnit;
                diagnosticBeforeWeapon = probeWeapon;
                diagnosticBeforeDetail = probeDetail;
            }
        }
        // Persistent support grip production evidence (root slot-0 invocation
        // of the stereo pair only: whichever slot-0 view interpolates first
        // resolves, the other eye reuses the pair's frozen decision, and a
        // later invocation of the same pair must not overwrite it). ODST keeps
        // its OWN reader/timing: the before/after reads are ODST's role bytes
        // +276/+277 with ODST's FP agreement record, never Halo 3's layout. The
        // after-read lands IMMEDIATELY after the native original and BEFORE any
        // MCCVR support-dependent transform. Read-only with respect
        // to the durable relationship; PG off: no read, no publication.
        const bool supportRootInvocation = g_config.persistent_support_grip && slot == 0 &&
                                           !(g_fpStereoSolveScope.armed && g_fpStereoSolveScope.supportGripResolved);
        PrimaryWeaponEvidence supportBefore{};
        const bool supportBeforeOk =
            supportRootInvocation && Game_ReadPrimaryWeaponEvidence(GameTitle::Halo3ODST, supportBefore);
        uint32_t muzzleUnit = UINT32_MAX, muzzleWeapon = UINT32_MAX;
        const bool muzzleOwner = OdstCaptureMuzzleOwner(view, slot, muzzleUnit, muzzleWeapon);
        bool result = false;
        FpInterpolateFn original = reinterpret_cast<FpInterpolateFn>(g_odstCamera.originalFpInterpolate);
        if (original)
            result = original(view, id, slot, outBones, outCount);
        if (supportRootInvocation)
        {
            PrimaryWeaponEvidence supportAfter{};
            const bool supportAfterOk = Game_ReadPrimaryWeaponEvidence(GameTitle::Halo3ODST, supportAfter);
            const SupportInvocationResolution resolution = Game_ResolveSupportInvocation(
                GameTitle::Halo3ODST, supportBefore, supportBeforeOk, supportAfter, supportAfterOk, true);
            // One decision per stereo pair, frozen in the ODST context: both
            // eyes then present and solve with the same attachment, and the
            // palette cache key distinguishes support-attached from detached
            // (the shared FpStereoPaletteCache key in
            // ReconstructVisiblePaletteSource). Both observed transition forms
            // fail closed here: an unstable before/after pair resolves Unknown
            // (never trusted), and a stable B-before-Capture resolves a
            // KnownPresent owner that is not the relationship owner.
            if (g_fpStereoSolveScope.armed)
            {
                g_fpStereoSolveScope.supportGripAttached = resolution.trusted;
                g_fpStereoSolveScope.supportGripResolved = true;
            }
        }
        if (g_scopeRenderActive.load(std::memory_order_acquire))
        {
            if (slot == 0 || slot == 1)
            {
                auto& scopeContext = g_fpInterpolationContexts[slot];
                scopeContext = {};
                if (view == 0 && result && outBones && outCount && *outBones && *outCount > 0 && *outCount <= 64)
                {
                    scopeContext.source = *outBones;
                    scopeContext.count = *outCount;
                    scopeContext.slot = slot;
                    scopeContext.valid = true;
                }
            }
            return result;
        }
        const bool ownsWeapons = g_odstCamera.armed.load(std::memory_order_acquire) &&
                                 !g_odstCamera.teardownRequested.load(std::memory_order_acquire) &&
                                 g_enabled.load(std::memory_order_relaxed);
        if ((slot == 0 || slot == 1) && ownsWeapons)
        {
            g_fpInterpolationContexts[slot] = {};
            OdstFpSkeletonLayout layout{};
            const bool haveBones = result && outBones && outCount && *outBones;
            const bool layoutOk = haveBones && ComputeOdstFpSkeletonLayout(*outCount, layout);
            PublishOdstFpLayoutSelfCheck(slot, haveBones ? *outCount : -1, layoutOk, layout);
            if (layoutOk)
            {
                const int count = *outCount;
                FpInterpolationContext& context = g_fpInterpolationContexts[slot];
                context.source = *outBones;
                context.count = count;
                context.generation = g_odstRuntimeGeneration.load(std::memory_order_acquire);
                uint32_t currentUnit = UINT32_MAX, currentWeapon = UINT32_MAX;
                if (muzzleOwner && OdstCaptureMuzzleOwner(view, slot, currentUnit, currentWeapon) &&
                    currentUnit == muzzleUnit && currentWeapon == muzzleWeapon)
                {
                    context.muzzleUnit = muzzleUnit;
                    context.muzzleWeapon = muzzleWeapon;
                }
                context.player = view;
                context.slot = slot;
                context.wrist = layout.rightWrist;
                context.cameraControl = layout.cameraControl;
                context.elbow = layout.rightElbow;
                context.shoulder = layout.rightShoulder;
                context.wristDescendants = layout.rightHandAndWeaponDescendants;
                context.lWrist = layout.leftWrist;
                context.lElbow = layout.leftElbow;
                context.lShoulder = layout.leftShoulder;
                context.lWristDescendants = layout.leftHandDescendants;
                context.valid = true;
                memcpy(g_fpUnmodifiedInterpolations[slot], *outBones, static_cast<size_t>(count) * sizeof(BoneMatrix));
                if (slot == 1 && view == 0)
                    VR_ObserveSecondaryWeaponPresentation(
                        GameTitle::Halo3ODST, g_odstRuntimeGeneration.load(std::memory_order_acquire));
                MeasureOdstAuthoredBarrel(slot, g_fpUnmodifiedInterpolations[slot], layout.rightWrist);
                BoneMatrix root{};
                if (BuildOdstCenterFpRoot(slot, root))
                    ApplyControllerToMarkerBonesFromRoot(
                        root, *outBones, count, layout.rightWrist, layout.leftWrist, slot == 1);
            }
        }
        else if (slot == 0 || slot == 1)
            g_fpInterpolationContexts[slot] = {};
        // Diagnostic-only stable commit, mirroring the existing ownership
        // stable point ungated: inventory identity plus engine FP record
        // agreement (bit2) must survive the interpolation unchanged.
        if (diagnosticArmed && result && diagnosticBeforeWeapon != UINT32_MAX && (diagnosticBeforeDetail & 0x4u) != 0)
        {
            uint32_t afterUnit = UINT32_MAX, afterWeapon = UINT32_MAX;
            uint32_t afterDetail = 0;
            if (Game_DiagnosticReadPrimaryWeapon(GameTitle::Halo3ODST, afterUnit, afterWeapon, afterDetail) &&
                (afterDetail & 0x4u) != 0 && afterUnit == diagnosticBeforeUnit && afterWeapon == diagnosticBeforeWeapon)
                PublishOdstFpDiagnostic(WeaponOrderEventKind::FpWeaponCommit, WeaponOrderEventStatus::Success,
                    g_fpStereoSolveScope.armed ? g_fpStereoSolveScope.anatomicalTracking.serial : 0, afterUnit,
                    afterWeapon, uint64_t(slot), afterDetail);
        }
        return result;
    }

    __declspec(noinline) bool __fastcall OdstFpInterpolateWeaponHook(
        int view, int id, int slot, BoneMatrix** bones, int* count)
    {
        g_odstCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        bool result = false;
        __try
        {
            result = OdstFpInterpolateWeaponBody(view, id, slot, bones, count);
        }
        __finally
        {
            g_odstCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
        return result;
    }

    void OdstFpVisiblePaletteWeaponBody(uint16_t tag, const BoneMatrix* root, BoneMatrix* destination, uintptr_t unused,
        const BoneMatrix* source, const int32_t* boneMap)
    {
        const uint32_t collisionGeneration = g_odstRuntimeGeneration.load(std::memory_order_acquire);
        FpInterpolationContext context{};
        for (FpInterpolationContext& candidate : g_fpInterpolationContexts)
            if (candidate.valid && source && candidate.source == source)
            {
                context = candidate;
                candidate.valid = false;
                break;
            }

        if (g_scopeRenderActive.load(std::memory_order_acquire))
        {
            const BoneMatrix* hidden = source;
            if (context.valid && source && context.count > 0 && context.count <= 64)
            {
                std::memcpy(g_scopeHiddenPalette, source, context.count * sizeof(BoneMatrix));
                for (int i = 0; i < context.count; ++i)
                    g_scopeHiddenPalette[i].scale = .0001f;
                hidden = g_scopeHiddenPalette;
            }
            auto original = reinterpret_cast<FpVisiblePaletteFn>(g_odstCamera.originalFpVisiblePalette);
            if (original)
                original(tag, root, destination, unused, hidden, boneMap);
            return;
        }
        (void)LegacyObserveWeaponRenderModel(GameTitle::Halo3ODST, tag, collisionGeneration, source, boneMap);
        const BoneMatrix* selectedSource = source;
        if (context.valid && source == context.source)
            selectedSource = g_fpUnmodifiedInterpolations[context.slot];
        bool reconstructed = false;
        if (root && source)
        {
            if (context.valid && isfinite(root->scale) && fabsf(root->scale) > 0.001f)
                g_odstFpRootScale[context.slot] = root->scale;
            reconstructed = ReconstructVisiblePaletteSource(tag, context, *root, source, selectedSource);
        }
        FpInterpolationContext contactContext = context;
        if (reconstructed && root && selectedSource == g_fpPaletteScratch &&
            g_fpStereoSolveScope.anatomicalTracking.handAlignment)
        {
            const bool routed =
                LegacyRouteLeftHandedPalette(GameTitle::Halo3ODST, tag, boneMap, *root, context, contactContext);
            (routed ? g_legacyAnatomicalApplications[1] : g_legacyAnatomicalRefusals[1])
                .fetch_add(1, std::memory_order_relaxed);
        }
        if (reconstructed && root && selectedSource == g_fpPaletteScratch)
        {
            LegacyPublishReloadTarget(
                GameTitle::Halo3ODST, tag, collisionGeneration, contactContext, *root, selectedSource, boneMap);
            LegacyPublishWorldCollisionVolumes(GameTitle::Halo3ODST, tag, contactContext, *root, selectedSource,
                LegacyCollisionIgnoredObject(GameTitle::Halo3ODST), collisionGeneration);
        }
        if (g_config.floating_hands && reconstructed && context.valid && selectedSource == g_fpPaletteScratch &&
            context.count > 0 && context.count <= 64)
            legacy_floating_hands::Apply(
                LegacyFloatingHandsLayout(context), g_fpPaletteScratch, g_config.floating_hands_collapse_at_wrist);
        if (g_scopeRenderActive.load(std::memory_order_acquire) && context.valid && selectedSource &&
            context.count > 0 && context.count <= 64)
        {
            memcpy(g_scopeHiddenPalette, selectedSource, static_cast<size_t>(context.count) * sizeof(BoneMatrix));
            for (int i = 0; i < context.count; ++i)
                g_scopeHiddenPalette[i].scale = 0.0001f;
            selectedSource = g_scopeHiddenPalette;
        }
        FpVisiblePaletteFn original = reinterpret_cast<FpVisiblePaletteFn>(g_odstCamera.originalFpVisiblePalette);
        if (original)
            original(tag, root, destination, unused, selectedSource, boneMap);
        if (original && reconstructed && selectedSource == g_fpPaletteScratch &&
            !g_scopeRenderActive.load(std::memory_order_acquire))
        {
            OdstPublishMuzzlePalette(tag, context, destination, collisionGeneration);
            LegacyApplyVisualHandOffsets(GameTitle::Halo3ODST, tag, boneMap, context, destination);
            OdstApplyControllerFingers(tag, context, destination, collisionGeneration);
        }
    }

    __declspec(noinline) void __fastcall OdstFpVisiblePaletteWeaponHook(uint16_t tag, const BoneMatrix* root,
        BoneMatrix* destination, uintptr_t unused, const BoneMatrix* source, const int32_t* boneMap)
    {
        g_odstCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            OdstFpVisiblePaletteWeaponBody(tag, root, destination, unused, source, boneMap);
        }
        __finally
        {
            g_odstCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    // ---- Full-parity diagnostics: non-first-person camera capture ----------
    // When slot 0 renders a live camera that is not the proven first-person
    // mode (the third-person death-cam, vehicles, turrets, cutscenes), the core
    // now renders it stock and stays armed instead of tearing down. This
    // log-only capture records that camera's field layout so the next build can
    // enable a stereo redirect for it with ODST evidence rather than a guess.
    // Publish is atomic-only from the hot render hook; the 50 ms worker emits.
    struct OdstNonFpCameraCapture
    {
        std::atomic<uint32_t> seq{0}; // even = stable, odd = mid-write
        std::atomic<uint32_t> modeFlags{0};
        std::atomic<uint32_t> arrayOffset{0};
        std::atomic<uint32_t> flags{0}; // see the OdstNonFpFlag bits below
        std::atomic<float> fpBlend{0.0f};
        std::atomic<float> verticalOffset{0.0f};
        std::atomic<float> verticalFov{0.0f};
        std::atomic<float> referenceFov{0.0f};
        std::atomic<float> nearClip{0.0f};
        std::atomic<float> farClip{0.0f};
    };
    enum OdstNonFpFlag : uint32_t
    {
        OdstNonFpTailValid = 1u << 0,
        OdstNonFpNestedMatch = 1u << 1,
        OdstNonFpActive = 1u << 2,
        OdstNonFpPlainPerspective = 1u << 3, // no oblique/custom projection,
                                             // ordered bounds, valid clips
        // Granular breakdown of the exact OdstCompactCameraIsStereoRedirectable
        // sub-checks, so a flat cutscene camera tells us precisely which field
        // disqualifies it rather than a single YES/NO.
        OdstNonFpModeZero = 1u << 4,       // +0x24 mode flags == 0
        OdstNonFpVoffFinite = 1u << 5,     // +0x34 observer offset is finite
        OdstNonFpFovInRange = 1u << 6,     // vertical/reference FOV in range
        OdstNonFpObliqueZero = 1u << 7,    // +0x6C..+0x7B oblique plane zero
        OdstNonFpCustomProjZero = 1u << 8, // +0x7C enable + data zero
        OdstNonFpBoundsOrdered = 1u << 9,  // window/render/active bounds
        OdstNonFpClipsValid = 1u << 10,    // near>0, far>near, finite
    };
    constinit OdstNonFpCameraCapture g_odstNonFpCameraCapture;
    constinit std::atomic<uint32_t> g_odstNonFpCameraLoggedSeq{0};
    // Sentinel modeFlags so a fresh non-FP camera re-captures after any FP frame.
    constexpr uint32_t kOdstNonFpNoCapture = 0xFFFFFFFFu;
    constinit std::atomic<uint32_t> g_odstNonFpCameraLastMode{kOdstNonFpNoCapture};

    // ---- Cutscene diagnostic: why a slot-0 render did NOT stereo-redirect -----
    // The non-FP capture above only fires for an active slot-0 camera whose
    // redirect gate failed. It never sees the cases most likely to make the
    // opening ODST cutscene flat: the private core still disarmed (arming
    // timing), OpenXR withholding presentation, an unresolved camera array, or a
    // cutscene rendered on a foreign (non-slot-0) camera. This lightweight
    // read-only publisher records that reason. Published atomically from the hot
    // render hook; the 50 ms worker emits one line per distinct reason.
    enum class OdstRenderSkipReason : uint32_t
    {
        None = 0,     // last frame was stereo-redirected
        NotArmed = 1, // core installed but not yet armed (debounce)
        TeardownRequested = 2,
        Unfocused = 3,        // VR_ShouldRenderPreparedFrame() == false
        ArrayUnavailable = 4, // gunCameraArray unresolved / view below array
        NotStrideAligned = 5, // view offset is not a clean slot boundary
        ForeignSlot = 6,      // active render on a slot other than 0
    };
    struct OdstRenderSkipDiag
    {
        std::atomic<uint32_t> seq{0}; // even = stable, odd = mid-write
        std::atomic<uint32_t> reason{0};
        std::atomic<uint32_t> arrayOffset{0};
    };
    constinit OdstRenderSkipDiag g_odstRenderSkipDiag;
    constinit std::atomic<uint32_t> g_odstRenderSkipLoggedSeq{0};
    constexpr uint32_t kOdstRenderSkipNone = static_cast<uint32_t>(OdstRenderSkipReason::None);
    constinit std::atomic<uint32_t> g_odstRenderSkipLastReason{kOdstRenderSkipNone};

    void PublishOdstRenderSkip(OdstRenderSkipReason reason, uint32_t arrayOffset)
    {
        const uint32_t r = static_cast<uint32_t>(reason);
        if (g_odstRenderSkipLastReason.load(std::memory_order_relaxed) == r)
            return; // steady state does not burst; a new reason re-publishes
        OdstRenderSkipDiag& d = g_odstRenderSkipDiag;
        d.seq.fetch_add(1, std::memory_order_acq_rel); // -> odd (writing)
        d.reason.store(r, std::memory_order_relaxed);
        d.arrayOffset.store(arrayOffset, std::memory_order_relaxed);
        d.seq.fetch_add(1, std::memory_order_release); // -> even (stable)
        g_odstRenderSkipLastReason.store(r, std::memory_order_relaxed);
    }

    // Called from the hot render hook on a non-FP slot-0 frame. Reads only the
    // already-bounds-checked view object (slot < 4, offsets < viewStride) and
    // never dereferences the nested-source pointer -- it only compares its
    // value. Deduped by modeFlags so full work happens at most once per distinct
    // non-FP camera kind between first-person frames.
    void OdstCaptureNonFpCamera(void* view, uintptr_t arrayOffset)
    {
        const auto& layout = kOdstCameraProfile.layout;
        const char* bytes = static_cast<const char*>(view);
        const char* compact = bytes + layout.rootCurrentCompact;
        const uint32_t modeFlags = *reinterpret_cast<const uint32_t*>(compact + layout.compactModeFlags);
        if (g_odstNonFpCameraLastMode.load(std::memory_order_relaxed) == modeFlags)
            return;

        const auto readFloat = [compact](uintptr_t offset)
        { return *reinterpret_cast<const float*>(compact + offset); };
        const float* oblique = reinterpret_cast<const float*>(compact + layout.obliquePlane);
        const uint32_t customProjection = *reinterpret_cast<const uint32_t*>(compact + layout.customProjection);
        const float* customData = reinterpret_cast<const float*>(compact + layout.customProjectionData);
        const auto boundsOrdered = [compact](uintptr_t offset)
        {
            const int16_t* b = reinterpret_cast<const int16_t*>(compact + offset);
            return b[0] < b[2] && b[1] < b[3];
        };
        const float nearClip = readFloat(layout.nearClip);
        const float farClip = readFloat(layout.farClip);
        // Break the redirect gate into its individual sub-checks so a flat
        // cutscene camera reveals the exact disqualifying field, not a lone NO.
        const float verticalFov = readFloat(layout.verticalFov);
        const float referenceFov = readFloat(layout.referenceFov);
        const float verticalOffset = readFloat(layout.verticalOffset);
        const bool modeZero = modeFlags == 0;
        const bool voffFinite = isfinite(verticalOffset);
        const bool fovInRange = isfinite(verticalFov) && verticalFov > 0.0001f && verticalFov < 3.1415f &&
                                isfinite(referenceFov) && referenceFov > 0.0001f && referenceFov < 3.1415f;
        const bool obliqueZero = oblique[0] == 0.0f && oblique[1] == 0.0f && oblique[2] == 0.0f && oblique[3] == 0.0f;
        const bool customProjZero = customProjection == 0 && customData[0] == 0.0f && customData[1] == 0.0f &&
                                    customData[2] == 0.0f && customData[3] == 0.0f;
        const bool boundsOrderedAll = boundsOrdered(layout.windowBounds) && boundsOrdered(layout.renderBounds) &&
                                      boundsOrdered(layout.activeBounds);
        const bool clipsValid = isfinite(nearClip) && isfinite(farClip) && nearClip > 0.0f && farClip > nearClip;
        const bool plainPerspective = obliqueZero && customProjZero && boundsOrderedAll && clipsValid;

        const uintptr_t nestedSource = *reinterpret_cast<const uintptr_t*>(bytes + layout.nestedSourceCamera);
        uint32_t flags = 0;
        if (OdstSingleUserTailIsValid(view))
            flags |= OdstNonFpTailValid;
        if (nestedSource == reinterpret_cast<uintptr_t>(view) + layout.rootCurrentCompact)
            flags |= OdstNonFpNestedMatch;
        if (OdstCompactCameraIsActive(compact))
            flags |= OdstNonFpActive;
        if (plainPerspective)
            flags |= OdstNonFpPlainPerspective;
        if (modeZero)
            flags |= OdstNonFpModeZero;
        if (voffFinite)
            flags |= OdstNonFpVoffFinite;
        if (fovInRange)
            flags |= OdstNonFpFovInRange;
        if (obliqueZero)
            flags |= OdstNonFpObliqueZero;
        if (customProjZero)
            flags |= OdstNonFpCustomProjZero;
        if (boundsOrderedAll)
            flags |= OdstNonFpBoundsOrdered;
        if (clipsValid)
            flags |= OdstNonFpClipsValid;

        OdstNonFpCameraCapture& cap = g_odstNonFpCameraCapture;
        cap.seq.fetch_add(1, std::memory_order_acq_rel); // -> odd (writing)
        cap.modeFlags.store(modeFlags, std::memory_order_relaxed);
        cap.arrayOffset.store(static_cast<uint32_t>(arrayOffset), std::memory_order_relaxed);
        cap.flags.store(flags, std::memory_order_relaxed);
        cap.fpBlend.store(readFloat(layout.compactFpBlend), std::memory_order_relaxed);
        cap.verticalOffset.store(readFloat(layout.verticalOffset), std::memory_order_relaxed);
        cap.verticalFov.store(readFloat(layout.verticalFov), std::memory_order_relaxed);
        cap.referenceFov.store(readFloat(layout.referenceFov), std::memory_order_relaxed);
        cap.nearClip.store(nearClip, std::memory_order_relaxed);
        cap.farClip.store(farClip, std::memory_order_relaxed);
        cap.seq.fetch_add(1, std::memory_order_release); // -> even (stable)
        g_odstNonFpCameraLastMode.store(modeFlags, std::memory_order_relaxed);
    }
} // namespace
namespace odst_bridge
{
    // Called from the 50 ms worker (NOT a hot hook), so logging is safe here.
    void LogOdstNonFpCameraIfNew()
    {
        OdstNonFpCameraCapture& cap = g_odstNonFpCameraCapture;
        const uint32_t seq = cap.seq.load(std::memory_order_acquire);
        if ((seq & 1u) || seq == 0)
            return; // mid-write or nothing captured yet
        if (seq == g_odstNonFpCameraLoggedSeq.load(std::memory_order_relaxed))
            return; // already logged this capture
        const uint32_t modeFlags = cap.modeFlags.load(std::memory_order_relaxed);
        const uint32_t arrayOffset = cap.arrayOffset.load(std::memory_order_relaxed);
        const uint32_t flags = cap.flags.load(std::memory_order_relaxed);
        const float fpBlend = cap.fpBlend.load(std::memory_order_relaxed);
        const float voff = cap.verticalOffset.load(std::memory_order_relaxed);
        const float vfov = cap.verticalFov.load(std::memory_order_relaxed);
        const float rfov = cap.referenceFov.load(std::memory_order_relaxed);
        const float nearClip = cap.nearClip.load(std::memory_order_relaxed);
        const float farClip = cap.farClip.load(std::memory_order_relaxed);
        if (cap.seq.load(std::memory_order_acquire) != seq)
            return; // a newer capture started; re-read next tick
        const bool tail = (flags & OdstNonFpTailValid) != 0;
        const bool nested = (flags & OdstNonFpNestedMatch) != 0;
        const bool active = (flags & OdstNonFpActive) != 0;
        const bool plain = (flags & OdstNonFpPlainPerspective) != 0;
        // Decode the exact gate sub-checks. The redirect actually requires ALL
        // of: tail, nested, active, mode==0, FOV in range, finite observer offset,
        // oblique zero, custom projection zero, bounds ordered, clips valid.
        // List only the ones that FAILED so a flat cutscene names its cause.
        char why[192];
        why[0] = '\0';
        const auto appendIfFailed = [&](bool passed, const char* label)
        {
            if (passed)
                return;
            const size_t used = strlen(why);
            _snprintf_s(why + used, sizeof(why) - used, _TRUNCATE, "%s%s", used ? "," : "", label);
        };
        appendIfFailed(tail, "tail");
        appendIfFailed(nested, "nested");
        appendIfFailed(active, "active");
        appendIfFailed((flags & OdstNonFpModeZero) != 0, "mode");
        appendIfFailed((flags & OdstNonFpFovInRange) != 0, "fov");
        appendIfFailed((flags & OdstNonFpVoffFinite) != 0, "voff");
        appendIfFailed((flags & OdstNonFpObliqueZero) != 0, "oblique");
        appendIfFailed((flags & OdstNonFpCustomProjZero) != 0, "customProj");
        appendIfFailed((flags & OdstNonFpBoundsOrdered) != 0, "bounds");
        appendIfFailed((flags & OdstNonFpClipsValid) != 0, "clips");
        const bool redirectable = why[0] == '\0';
        char verdict[224];
        if (redirectable)
            _snprintf_s(verdict, sizeof(verdict), _TRUNCATE, "YES (plain slot-0 -- redirect candidate)");
        else
            _snprintf_s(verdict, sizeof(verdict), _TRUNCATE, "NO (render stock; fails: %s)", why);
        LOG("ODST NON-FP CAMERA: mode=0x%08X slot=%u tail=%d nested=%d "
            "active=%d plainPersp=%d blend=%.4f voff=%.4f vfov=%.4f rfov=%.4f "
            "near=%.4f far=%.1f -- stereo-redirectable = %s",
            modeFlags, arrayOffset, tail, nested, active, plain, fpBlend, voff, vfov, rfov, nearClip, farClip, verdict);
        g_odstNonFpCameraLoggedSeq.store(seq, std::memory_order_relaxed);
    }

    // Called from the 50 ms worker (NOT a hot hook), so logging is safe here.
    // Emits why a slot-0 render frame was not stereo-redirected in the cases the
    // non-FP capture above cannot see: the core not yet armed, teardown pending,
    // OpenXR unfocused, an unresolved array, or a foreign-slot camera. This is
    // the primary signal for whether the opening ODST cutscene is flat because
    // of arming timing versus a genuinely different camera object.
    void LogOdstRenderSkipIfNew()
    {
        OdstRenderSkipDiag& d = g_odstRenderSkipDiag;
        const uint32_t seq = d.seq.load(std::memory_order_acquire);
        if ((seq & 1u) || seq == 0)
            return; // mid-write or nothing captured yet
        if (seq == g_odstRenderSkipLoggedSeq.load(std::memory_order_relaxed))
            return; // already logged this reason
        const uint32_t reason = d.reason.load(std::memory_order_relaxed);
        const uint32_t arrayOffset = d.arrayOffset.load(std::memory_order_relaxed);
        if (d.seq.load(std::memory_order_acquire) != seq)
            return; // a newer publish started; re-read next tick
        const char* name = "unknown";
        switch (static_cast<OdstRenderSkipReason>(reason))
        {
        case OdstRenderSkipReason::None:
            name = "none(redirected)";
            break;
        case OdstRenderSkipReason::NotArmed:
            name = "core not armed (fresh-camera debounce)";
            break;
        case OdstRenderSkipReason::TeardownRequested:
            name = "teardown requested";
            break;
        case OdstRenderSkipReason::Unfocused:
            name = "OpenXR unfocused (no prepared frame)";
            break;
        case OdstRenderSkipReason::ArrayUnavailable:
            name = "camera array unresolved / view below array";
            break;
        case OdstRenderSkipReason::NotStrideAligned:
            name = "view offset not slot-aligned";
            break;
        case OdstRenderSkipReason::ForeignSlot:
            name = "active camera on a foreign slot (not slot 0)";
            break;
        }
        LOG("ODST RENDER SKIP: reason=%s arrayOffset=0x%X -- this frame renders "
            "stock 2D, not stereo",
            name, arrayOffset);
        g_odstRenderSkipLoggedSeq.store(seq, std::memory_order_relaxed);
    }

    // Called from the 50 ms worker (NOT a hot hook), so logging is safe here.
    // Emits the one-shot FP weapon-layout self-check published by the hot FP
    // interpolation hook: if the hands/gun stay on the head in the headset, this
    // line tells us whether the hook fired and whether the skeleton was accepted.
    void LogOdstFpLayoutSelfCheckIfNew()
    {
        OdstFpLayoutSelfCheck& sc = g_odstFpLayoutSelfCheck;
        const uint32_t key = sc.key.load(std::memory_order_acquire);
        if (key == 0)
            return; // the FP interpolation hook has not seen a weapon slot yet
        if (key == g_odstFpLayoutLoggedKey.load(std::memory_order_relaxed))
            return; // already logged this observation
        const int slot = sc.slot.load(std::memory_order_relaxed);
        const int nodeCount = sc.nodeCount.load(std::memory_order_relaxed);
        const int accepted = sc.accepted.load(std::memory_order_relaxed);
        const int wrist = sc.wrist.load(std::memory_order_relaxed);
        const int cameraControl = sc.cameraControl.load(std::memory_order_relaxed);
        if (sc.key.load(std::memory_order_acquire) != key)
            return; // a newer observation started; re-read next tick
        LOG("ODST FP WEAPON SELF-CHECK: interpolation hook fired slot=%d "
            "nodeCount=%d layoutAccepted=%s (need 39..64) wrist=%d "
            "cameraControl=%d -- hands/gun driven by controller = %s",
            slot, nodeCount, accepted ? "YES" : "NO", wrist, cameraControl,
            accepted ? "YES" : "NO (layout rejected -> stock pose on head)");
        g_odstFpLayoutLoggedKey.store(key, std::memory_order_relaxed);
    }

    // Cold-worker report for the bounded phase-level route check. Hot hooks only
    // increment atomics; no logging, COM discovery, allocation, or file I/O.
    void LogOdstNativeHudRouteOnce()
    {
        static bool logged = false;
        if (logged)
            return;
        unsigned completedPhaseScopes = 0, provenOmMatches = 0;
        unsigned exactCopyScopes = 0, copySubstitutions = 0;
        VR_GetNativeHudRouteStats(completedPhaseScopes, provenOmMatches, exactCopyScopes, copySubstitutions);
        if (completedPhaseScopes < 120)
            return;
        logged = true;
        LOG("ODST NATIVE HUD ROUTE: completedScopes=%u provenOmMatches=%u "
            "exactCopyScopes=%u copySubstitutions=%u -- %s",
            completedPhaseScopes, provenOmMatches, exactCopyScopes, copySubstitutions,
            copySubstitutions ? "target-1 snapshot sourced from the active eye cache"
            : exactCopyScopes ? "target-1 source identity did not match; stock copy retained"
                              : "exact target-1 copy was not observed; stock path retained");
    }
} // namespace odst_bridge
namespace
{
    // The scope camera vs the head, through VR_NoteScopeCamera.
    inline void OdstScopeLogCamera(const float* camera, const float* head)
    {
        VR_NoteScopeCamera(GameTitle::Halo3ODST, camera, head, cockpit_screen::kWorldUnitsPerMeter);
    }
    // The scope camera origin at the muzzle, or the head origin.
    inline const float* OdstScopeOrigin(const float* head, const float* barrel, float out[3])
    {
        return VR_ScopeCameraOrigin(GameTitle::Halo3ODST, cockpit_screen::kWorldUnitsPerMeter, barrel, out) ? out
                                                                                                            : head;
    }
#include "odst_scope.inl"

    __declspec(noinline) void __fastcall OdstRenderViewBody(void* view)
    {
        RenderViewFn original = g_odstCamera.originalRenderView;
        if (!original)
            return;
        // VR not engaged for this title at all (view null, mod disabled, or the
        // user toggled stereo off): pass through silently, no cutscene evidence.
        if (!view || !g_enabled.load(std::memory_order_relaxed) || !VR_IsStereoEnabled())
        {
            original(view);
            return;
        }
        // VR IS engaged but the private core is not presenting. This is the
        // arming-timing signal: if the opening cutscene is flat and one of these
        // fires, the intro renders before the fresh-camera debounce arms the core
        // (NotArmed) or during a teardown.
        const bool teardownRequested = g_odstCamera.teardownRequested.load(std::memory_order_acquire);
        if (teardownRequested || !g_odstCamera.armed.load(std::memory_order_acquire))
        {
            PublishOdstRenderSkip(
                teardownRequested ? OdstRenderSkipReason::TeardownRequested : OdstRenderSkipReason::NotArmed, 0);
            original(view);
            return;
        }

        if (EvaluateOdstStereoFrame(VR_ShouldRenderPreparedFrame()) == OdstStereoFrameAction::RenderStockWithoutCapture)
        {
            // OpenXR deliberately suppresses presentation while the headset is
            // unfocused. Preserve the installed hooks, render the desktop's
            // ordinary view once, and collect no eye-redirect failure evidence.
            PublishOdstRenderSkip(OdstRenderSkipReason::Unfocused, 0);
            original(view);
            return;
        }
        const auto& layout = kOdstCameraProfile.layout;
        const uintptr_t viewAddress = reinterpret_cast<uintptr_t>(view);
        const uintptr_t arrayAddress = g_odstCamera.gunCameraArray;
        if (!arrayAddress || viewAddress < arrayAddress)
        {
            PublishOdstRenderSkip(OdstRenderSkipReason::ArrayUnavailable, 0);
            original(view);
            return;
        }
        const uintptr_t arrayOffset = viewAddress - arrayAddress;
        if (arrayOffset % layout.viewStride != 0 || arrayOffset / layout.viewStride >= 4)
        {
            PublishOdstRenderSkip(OdstRenderSkipReason::NotStrideAligned, static_cast<uint32_t>(arrayOffset));
            original(view);
            return;
        }
        char* bytes = static_cast<char*>(view);
        char* camera = bytes + layout.rootCurrentCompact;
        // Slot 0's active plain-perspective cameras share the proven camera
        // layout. First-person/vehicles redirect the internal scene color;
        // blend-0 death renders are copied from ODST's direct backbuffer path.
        // Foreign slots and custom projections remain stock. A live render
        // frame is never itself a teardown trigger.
        const bool ownsPrimarySlot = arrayOffset == 0;
        const bool tailValid = ownsPrimarySlot && OdstSingleUserTailIsValid(view);
        const bool nestedMatch =
            ownsPrimarySlot && *reinterpret_cast<const uintptr_t*>(bytes + layout.nestedSourceCamera) ==
                                   viewAddress + layout.rootCurrentCompact;
        const bool redirectable = ownsPrimarySlot && OdstCompactCameraIsStereoRedirectable(camera);
        if (!OdstShouldStereoRedirect(ownsPrimarySlot, tailValid, nestedMatch, redirectable))
        {
            // Record the non-first-person camera's field layout (log-only) so a
            // future build can extend the redirect to it with ODST evidence.
            // A foreign (non-slot-0) active camera cannot be captured by that
            // slot-0 dump, so note it distinctly -- a cutscene rendered there is
            // why it would be flat.
            if (ownsPrimarySlot)
                OdstCaptureNonFpCamera(view, arrayOffset);
            else
                PublishOdstRenderSkip(OdstRenderSkipReason::ForeignSlot, static_cast<uint32_t>(arrayOffset));
            original(view);
            return;
        }
        // A redirected frame: allow a later unsupported/custom camera to
        // re-capture fresh diagnostics the next time, and let a later skip
        // reason re-log after a stretch of successful stereo frames.
        g_odstNonFpCameraLastMode.store(kOdstNonFpNoCapture, std::memory_order_relaxed);
        g_odstRenderSkipLastReason.store(kOdstRenderSkipNone, std::memory_order_relaxed);
        struct EyeRenderInput
        {
            float position[3]{};
            float orientation[4]{};
            float halfX = 0.0f;
            float halfY = 0.0f;
            OdstHalo3FovMatch fovMatch{};
        } eyeInputs[2];
        const bool cutsceneTheater = VR_IsCutsceneTheaterActive();
        bool eyeInputsValid = true;
        for (int eye = 0; eye < 2; ++eye)
        {
            float fov[4]{};
            eyeInputsValid = VR_GetEyeViewOffset(eye, eyeInputs[eye].position, eyeInputs[eye].orientation) &&
                             VR_GetEyeFov(eye, fov) && eyeInputsValid;
            ApplyCutsceneTheaterEyeTransform(
                cutsceneTheater, g_config.cutscene_theater_depth, eyeInputs[eye].position, eyeInputs[eye].orientation);
            for (float component : eyeInputs[eye].position)
                eyeInputsValid = isfinite(component) && eyeInputsValid;
            float orientationLength = 0.0f;
            for (float component : eyeInputs[eye].orientation)
            {
                eyeInputsValid = isfinite(component) && eyeInputsValid;
                orientationLength += component * component;
            }
            for (float angle : fov)
                eyeInputsValid = isfinite(angle) && eyeInputsValid;
            eyeInputs[eye].halfX = fmaxf(-fov[0], fov[1]);
            eyeInputs[eye].halfY = fmaxf(fov[2], -fov[3]);
            eyeInputsValid =
                orientationLength > 0.5f && orientationLength < 1.5f && eyeInputs[eye].halfX > 0.01f &&
                eyeInputs[eye].halfX < 1.55f && eyeInputs[eye].halfY > 0.01f && eyeInputs[eye].halfY < 1.55f &&
                ComputeOdstHalo3FovMatch(eyeInputs[eye].halfX, eyeInputs[eye].halfY, eyeInputs[eye].fovMatch) &&
                eyeInputsValid;
        }
        if (!eyeInputsValid)
        {
            OdstRequestFallback(OdstFallbackReason::EyeRedirectUnavailable);
            original(view);
            return;
        }
        if (!OdstCameraArraySupportsStereoRedirect(arrayAddress))
        {
            // Eye-location calls above are outside the game camera owner. Check
            // the complete four-slot/single-user invariant again, accepting
            // either proven render path, before any camera byte is mutated.
            original(view);
            return;
        }
        alignas(16) unsigned char savedCompact[0x90];
        alignas(16) unsigned char savedDerived[0xC0];
        alignas(16) unsigned char savedSecondaryCompact[0x90];
        alignas(16) unsigned char savedSecondaryDerived[0xC0];
        alignas(16) unsigned char savedNestedCompact[0x90];
        alignas(16) unsigned char savedNestedDerived[0xC0];
        alignas(16) unsigned char savedNestedSecondaryCompact[0x90];
        alignas(16) unsigned char savedNestedSecondaryDerived[0xC0];
        memcpy(savedCompact, camera, layout.compactSize);
        memcpy(savedDerived, bytes + layout.rootCurrentDerived, layout.derivedSize);
        memcpy(savedSecondaryCompact, bytes + layout.rootSecondaryCompact, layout.compactSize);
        memcpy(savedSecondaryDerived, bytes + layout.rootSecondaryDerived, layout.derivedSize);
        memcpy(savedNestedCompact, bytes + layout.nestedCurrentCompact, layout.compactSize);
        memcpy(savedNestedDerived, bytes + layout.nestedCurrentDerived, layout.derivedSize);
        memcpy(savedNestedSecondaryCompact, bytes + layout.nestedSecondaryCompact, layout.compactSize);
        memcpy(savedNestedSecondaryDerived, bytes + layout.nestedSecondaryDerived, layout.derivedSize);

        const float* authoredProjection = reinterpret_cast<const float*>(savedDerived + layout.projectionMatrix);
        const float authoredAspect =
            CutsceneTheaterAspectFromProjectionScales(authoredProjection[0], authoredProjection[5]);
        const uint32_t theaterGeneration = g_odstRuntimeGeneration.load(std::memory_order_relaxed);
        if (theaterGeneration && authoredAspect > 0.0f && (!cutsceneTheater || CinematicFovPolicyStockReady()))
        {
            TitleAdapter_PublishCutsceneTheaterProjection(
                GameTitle::Halo3ODST, theaterGeneration, authoredAspect, GetTickCount64());
        }
        const bool preserveAuthoredProjection =
            UseAuthoredCutsceneProjection(cutsceneTheater, authoredAspect) && CinematicFovPolicyStockReady();

        const float* savedForward = reinterpret_cast<const float*>(savedCompact + layout.compactForward);
        const float* savedUp = reinterpret_cast<const float*>(savedCompact + layout.compactUp);
        const float savedRight[3] = {
            savedForward[1] * savedUp[2] - savedForward[2] * savedUp[1],
            savedForward[2] * savedUp[0] - savedForward[0] * savedUp[2],
            savedForward[0] * savedUp[1] - savedForward[1] * savedUp[0],
        };

        bool capturesOk = true;
        // Exactly one articulated pose per stereo pair, matching Halo 3. The
        // second eye reprojects the cached center-root solve into its own root.
        // Ordinary ODST dual-wield expansion is out of scope. Its independent
        // controller publisher remains dormant; authored barrel aiming stays.
        g_fpStereoSolveScope = {};
        g_fpStereoSolveScope.armed = true;
        g_fpStereoSolveScope.twoHandAimActive = VR_IsTwoHandAiming();
        (void)VR_GetContactTrackingSnapshot(g_fpStereoSolveScope.anatomicalTracking);
        const int firstEye = g_config.right_eye_first ? 1 : 0;
        for (int pass = 0; pass < 2; ++pass)
        {
            const int eye = pass == 0 ? firstEye : 1 - firstEye;
            g_stereoEye.store(eye, std::memory_order_release);
            VR_BeginRasterEye(eye);
            memcpy(camera, savedCompact, layout.compactSize);
            memcpy(bytes + layout.rootCurrentDerived, savedDerived, layout.derivedSize);
            memcpy(bytes + layout.rootSecondaryCompact, savedSecondaryCompact, layout.compactSize);
            memcpy(bytes + layout.rootSecondaryDerived, savedSecondaryDerived, layout.derivedSize);
            memcpy(bytes + layout.nestedCurrentCompact, savedNestedCompact, layout.compactSize);
            memcpy(bytes + layout.nestedCurrentDerived, savedNestedDerived, layout.derivedSize);
            memcpy(bytes + layout.nestedSecondaryCompact, savedNestedSecondaryCompact, layout.compactSize);
            memcpy(bytes + layout.nestedSecondaryDerived, savedNestedSecondaryDerived, layout.derivedSize);

            float* position = reinterpret_cast<float*>(camera + layout.compactPosition);
            const float* eyePosition = eyeInputs[eye].position;
            const float* eyeOrientation = eyeInputs[eye].orientation;
            for (int axis = 0; axis < 3; ++axis)
                position[axis] += (savedRight[axis] * eyePosition[0] + savedUp[axis] * eyePosition[1] -
                                      savedForward[axis] * eyePosition[2]) *
                                  kOdstWorldUnitsPerMeter;

            const float sinHalf = sqrtf(eyeOrientation[0] * eyeOrientation[0] + eyeOrientation[1] * eyeOrientation[1] +
                                        eyeOrientation[2] * eyeOrientation[2]);
            if (sinHalf > 1e-5f)
            {
                float angle = 2.0f * atan2f(sinHalf, eyeOrientation[3]);
                if (angle > 3.14159265f)
                    angle -= 6.2831853f;
                const float axis[3] = {
                    (eyeOrientation[0] / sinHalf) * savedRight[0] + (eyeOrientation[1] / sinHalf) * savedUp[0] -
                        (eyeOrientation[2] / sinHalf) * savedForward[0],
                    (eyeOrientation[0] / sinHalf) * savedRight[1] + (eyeOrientation[1] / sinHalf) * savedUp[1] -
                        (eyeOrientation[2] / sinHalf) * savedForward[1],
                    (eyeOrientation[0] / sinHalf) * savedRight[2] + (eyeOrientation[1] / sinHalf) * savedUp[2] -
                        (eyeOrientation[2] / sinHalf) * savedForward[2],
                };
                const float cosAngle = cosf(angle), sinAngle = sinf(angle);
                RotateAboutAxis(reinterpret_cast<float*>(camera + layout.compactForward), axis, cosAngle, sinAngle);
                RotateAboutAxis(reinterpret_cast<float*>(camera + layout.compactUp), axis, cosAngle, sinAngle);
            }

            float halfX = eyeInputs[eye].halfX;
            float halfY = eyeInputs[eye].halfY;
            const OdstHalo3FovMatch& fovMatch = eyeInputs[eye].fovMatch;
            if (!preserveAuthoredProjection)
            {
                *reinterpret_cast<float*>(camera + layout.verticalFov) = fovMatch.compactVerticalInput;
                *reinterpret_cast<float*>(camera + layout.referenceFov) = fovMatch.compactReferenceInput;
            }

            alignas(16) unsigned char temporary[0x40]{};
            g_odstCamera.buildViewport(camera, temporary);
            g_odstCamera.buildMatrices(camera, temporary, bytes + layout.rootCurrentDerived, 0.0f);
            float* projection = reinterpret_cast<float*>(bytes + layout.rootCurrentDerived + layout.projectionMatrix);
            if (preserveAuthoredProjection)
            {
                const ReachProjectionHalfFovs authoredFov = DecodeReachProjectionHalfFovs(projection[0], projection[5]);
                if (authoredFov.valid)
                {
                    halfX = authoredFov.horizontal;
                    halfY = authoredFov.vertical;
                }
            }
            else
            {
                projection[0] = fovMatch.projectionX;
                projection[5] = fovMatch.projectionY;
            }
            static std::atomic<unsigned> loggedFovEyes{0};
            const unsigned eyeBit = 1u << eye;
            if (!(loggedFovEyes.fetch_or(eyeBit, std::memory_order_relaxed) & eyeBit))
            {
                LOG("ODST FOV match eye %d: compact pair %.4f/%.4f -> "
                    "final projection %.5f/%.5f (Halo 3 numeric path)",
                    eye, fovMatch.compactVerticalInput, fovMatch.compactReferenceInput, projection[0], projection[5]);
            }
            // Optional DLSS: same contract as Halo 3. The derived block is
            // snapshotted whole below, so the FP driver hooks carry the
            // jittered centre terms without extra work here.
            {
                float jitterNdcX = 0.0f, jitterNdcY = 0.0f;
                if (!cutsceneTheater && VR_GetEyeJitter(eye, jitterNdcX, jitterNdcY))
                {
                    const float centerSign = dlss::ProjectionCenterSign(projection);
                    projection[8] += jitterNdcX * centerSign;
                    projection[9] += jitterNdcY * centerSign;
                }
                VrEyeCameraSample sample{};
                memcpy(sample.position, camera + layout.compactPosition, sizeof(sample.position));
                memcpy(sample.forward, camera + layout.compactForward, sizeof(sample.forward));
                memcpy(sample.up, camera + layout.compactUp, sizeof(sample.up));
                memcpy(sample.projection, projection, sizeof(sample.projection));
                sample.jitterNdcX = jitterNdcX;
                sample.jitterNdcY = jitterNdcY;
                VR_PublishEyeCamera(eye, sample);
            }
            g_odstRenderHalfFovX[eye].store(halfX, std::memory_order_relaxed);
            g_odstRenderHalfFovY[eye].store(halfY, std::memory_order_relaxed);

            memcpy(bytes + layout.rootSecondaryCompact, camera, layout.compactSize);
            memcpy(bytes + layout.rootSecondaryDerived, bytes + layout.rootCurrentDerived, layout.derivedSize);
            if (g_odstCamera.fpCameraUpload)
                g_odstCamera.fpCameraUpload(bytes + layout.rootSecondaryCompact, bytes + layout.rootSecondaryDerived);
            memcpy(g_odstCamera.eyeCompactCamera, camera, layout.compactSize);
            memcpy(g_odstCamera.eyeDerivedBlock, bytes + layout.rootCurrentDerived, layout.derivedSize);
            g_odstCamera.eyeView.store(view, std::memory_order_release);
            g_odstPreparingEyeHud = true;
            g_odstCamera.prepareView(view, 0);
            g_odstPreparingEyeHud = false;
            original(view);
            g_odstCamera.eyeView.store(nullptr, std::memory_order_release);
            bool captured = VR_CaptureRenderedEye(eye);
            if (!captured)
                captured = VR_CaptureBackbufferEye(eye);
            capturesOk = captured && capturesOk;
            VR_EndRasterEye();
        }

        g_fpStereoSolveScope.armed = false;
        g_stereoEye.store(-1, std::memory_order_release);
        g_odstCamera.eyeView.store(nullptr, std::memory_order_release);
        memcpy(camera, savedCompact, layout.compactSize);
        memcpy(bytes + layout.rootCurrentDerived, savedDerived, layout.derivedSize);
        memcpy(bytes + layout.rootSecondaryCompact, savedSecondaryCompact, layout.compactSize);
        memcpy(bytes + layout.rootSecondaryDerived, savedSecondaryDerived, layout.derivedSize);
        memcpy(bytes + layout.nestedCurrentCompact, savedNestedCompact, layout.compactSize);
        memcpy(bytes + layout.nestedCurrentDerived, savedNestedDerived, layout.derivedSize);
        memcpy(bytes + layout.nestedSecondaryCompact, savedNestedSecondaryCompact, layout.compactSize);
        memcpy(bytes + layout.nestedSecondaryDerived, savedNestedSecondaryDerived, layout.derivedSize);

        if (capturesOk && !cutsceneTheater)
            RenderOdstScope(view);

        if (capturesOk)
            g_odstCamera.captureFailures.store(0, std::memory_order_release);
        else
        {
            // Match Halo 3: a live render capture miss never dismantles the
            // title hooks. ODST can switch to its death renderer while fpBlend
            // still reports first person, so mode-based failure thresholds are
            // not reliable. Preserve the last valid eye pair until either the
            // direct backbuffer capture or normal scene-color capture returns.
            g_odstCamera.captureFailures.fetch_add(1, std::memory_order_acq_rel);
        }
    }

    __declspec(noinline) void __fastcall OdstRenderViewHook(void* view)
    {
        g_odstCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        g_odstCamera.activeRenderCallbacks.fetch_add(1, std::memory_order_acq_rel);
        OdstRenderViewBody(view);
        g_odstCamera.activeRenderCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        g_odstCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }

    // Both titles schedule native CHUD during prepareView. Keep ODST's calls in
    // that exact engine-owned scope and order, but bind their active eye cache
    // while the title submits the phase. Flat/shell and unrelated callers remain
    // stock because only this render thread sets g_odstPreparingEyeHud.
    void OdstRenderNativeHudPhase(OdstHudPhaseFn original, int userIndex)
    {
        if (!original)
            return;
        const int eye = g_stereoEye.load(std::memory_order_acquire);
        const bool route = g_odstPreparingEyeHud && eye >= 0 && eye <= 1 &&
                           g_odstCamera.eyeView.load(std::memory_order_acquire) != nullptr &&
                           g_enabled.load(std::memory_order_relaxed) && VR_IsStereoEnabled() &&
                           g_odstCamera.armed.load(std::memory_order_acquire) &&
                           !g_odstCamera.teardownRequested.load(std::memory_order_acquire) &&
                           VR_ShouldRenderPreparedFrame();
        if (!route || !VR_BeginNativeHudEyeDraw(eye))
        {
            original(userIndex);
            return;
        }
        original(userIndex);
        VR_EndNativeHudEyeDraw();
    }
    __declspec(noinline) void __fastcall OdstHudPhasePrimaryHook(int userIndex)
    {
        g_odstCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        OdstRenderNativeHudPhase(g_odstCamera.originalHudPhasePrimary, userIndex);
        g_odstCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }

    __declspec(noinline) void __fastcall OdstHudPhaseSecondaryHook(int userIndex)
    {
        g_odstCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        OdstRenderNativeHudPhase(g_odstCamera.originalHudPhaseSecondary, userIndex);
        g_odstCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }

    __declspec(noinline) uint32_t __fastcall OdstHudTargetCopyHook(int sourceTargetId, int destinationTargetId)
    {
        g_odstCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        OdstHudTargetCopyFn original = g_odstCamera.originalHudTargetCopy;
        uint32_t result = 0;
        if (original)
        {
            // Static ODST evidence: secondary CHUD snapshots engine target 1
            // into its title-specific target 0x35 before later target-1 work.
            const bool expectedCopy = sourceTargetId == 1 && destinationTargetId == 0x35;
            if (expectedCopy)
                VR_BeginNativeHudTargetCopy();
            result = original(sourceTargetId, destinationTargetId);
            if (expectedCopy)
                VR_EndNativeHudTargetCopy();
        }
        g_odstCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        return result;
    }
    constinit const char* kOdstFpInterpolateSig =
        "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 "
        "41 57 48 83 EC 30 33 DB 49 63 E8 38 1D ?? ?? ?? ?? 4D 8B E1 44 8B FA";
    constinit const char* kOdstFpVisiblePaletteSig =
        "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 "
        "EC 20 48 8B 05 ?? ?? ?? ?? 49 8B F0 0F B7 C9 4C 8B F2";
    constinit const char* kOdstNativeWeaponIkDecisionSig = "40 84 ED 74 05 45 84 FF 75 04 84 DB 74 0F BA 03 00 00 00 "
                                                           "41 0F 28 D9 44 8D 42 FF EB 11";
    // Pinned retail ODST's unique cinematic user-input-constraint setter.
    // The RIP operand names ODST's engine TLS index and `mov r9d, imm32`
    // supplies its cinematic-camera TLS member. The fixed tail proves the
    // object type check plus current maximum-look-angle C8/CC/D0/D4 and
    // interpolation-rate D8/DC/E0/E4 layout used below.
    constinit const char* kOdstCinematicUserInputConstraintSig = "44 8B 05 ?? ?? ?? ?? 0F 28 E2 "
                                                                 "65 48 8B 04 25 58 00 00 00 41 B9 ?? 00 00 00 "
                                                                 "4A 8B 04 C0 4D 8B 0C 01 B8 05 00 00 00 "
                                                                 "66 41 3B 41 02 0F 85 ?? ?? ?? ?? "
                                                                 "F3 0F 10 15 ?? ?? ?? ?? 44 8D 40 FC "
                                                                 "0F BF 42 02 0F 57 C0 41 3B C8 0F 57 DB 0F 57 C9 "
                                                                 "44 0F 4D C1 F3 0F 2A C0 F3 41 0F 2A D8 "
                                                                 "F3 0F 59 C2 F3 41 0F 5C 81 C8 00 00 00";
    // The unique paired camera updater proves that +F0 is a positive remaining-
    // tick count: while positive it adds D8/DC/E0/E4 to C8/CC/D0/D4 and then
    // decrements +F0. This makes an active nonzero rate explicit pending look
    // freedom rather than stale storage.
    constinit const char* kOdstCinematicUserInputUpdateSig = "39 AB F0 00 00 00 7E ?? 48 8D 83 C8 00 00 00 "
                                                             "B9 04 00 00 00 F3 0F 10 40 10 F3 0F 58 00 "
                                                             "F3 0F 11 00 48 83 C0 04 49 2B CE 75 ?? "
                                                             "F3 0F 10 83 EC 00 00 00 F3 0F 58 83 E8 00 00 00 "
                                                             "FF 8B F0 00 00 00 F3 0F 11 83 E8 00 00 00";
    // Unique ODST-native wrappers that directly invoke chud_draw_widget.
    constinit const char* kOdstHudPhasePrimarySig = "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 "
                                                    "41 54 41 56 41 57 48 83 EC 40 48 63 F9";
    constinit const char* kOdstHudPhaseSecondarySig = "48 89 5C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 50 "
                                                      "48 63 D9 8B CB 0F 29 74 24 40";
    // ODST's engine target copy. The secondary phase calls it only after
    // selecting temporary target ID 0x35, with source ID 1.
    constinit const char* kOdstHudTargetCopySig = "48 63 C1 4C 8D 1D ?? ?? ?? ?? 33 C9 4C 63 D2 4C 8D 04 40 "
                                                  "8B C1 49 C1 E0 05 43 F7 04 18 00 04 00 00 74 05 43 8B 44 18 58";

    // ODST vehicle-detection identities.
    // Each was derived from the pinned halo3odst.dll and re-derived
    // independently; each matches exactly once here and ZERO times in
    // halo3.dll, so a cross-title mismatch cannot silently bind.
    //
    // Native unit_in_vehicle (halo3odst.dll+0x3C54CC). The wildcarded disp32
    // is the engine TLS index; the pinned `41 B8 20` is ODST's object-table
    // TLS slot and the trailing `62 02` is its unit seat word (+0x262) -
    // Halo 3's own bytes there are 0x38 and 0x4E 0x02.
    constinit const char* kOdstUnitInVehicleSig = "48 89 5C 24 08 57 48 83 EC 20 45 32 DB 41 83 C9 FF 41 3B C9 "
                                                  "0F 84 82 00 00 00 8B 15 ?? ?? ?? ?? 65 48 8B 04 25 58 00 00 00 "
                                                  "41 B8 20 00 00 00 0F B7 C9 48 8B 04 D0 4A 8B 14 00 48 8D 04 49";
    // player_mapping_get_unit_by_output_user (+0x109260). The +0xC8 tail is
    // load-bearing: a byte-identical sibling at +0x109230 returns the +0xB8
    // player-index array instead of the unit handle.
    // Pod look-around. All three are unique in
    // halo3odst.dll and absent from halo3.dll. The action-test accumulator is
    // [engine TLS + 0xD8] + user * 0x30, qword +0:
    //  reset: player_action_test_reset's evaluate (+0x2249DC) clears it;
    //  builder: the per-user action-test builder (+0x114C62) ORs into it;
    //  look: the same builder sets bits 7/8 from the facing pitch delta's sign
    //   and 9/10 from the yaw delta's (+0x114FD0).
    // Both TLS loads must decode to the vehicle binding's own TLS index.
    constinit const char* kOdstActionTestResetSig = "8B 0D ?? ?? ?? ?? 45 33 C0 65 48 8B 04 25 58 00 00 00 44 8B CA "
                                                    "41 BA D8 00 00 00 48 8B 04 C8 33 C9 49 83 CB FF 4D 8B 14 02 "
                                                    "48 8D 04 49 41 8B D3 48 03 C0 49 83 24 C2 00 49 83 64 C2 08 00";
    constinit const char* kOdstActionTestBuilderSig = "8B 0D ?? ?? ?? ?? BD 01 00 00 00 41 BB D8 00 00 00 4C 8D 14 76 "
                                                      "4C 8B 0C C8 4D 03 D2";
    constinit const char* kOdstActionTestLookBitsSig = "F3 0F 10 43 1C 0F 2F C1 76 08 4B 0F BA 2C D3 07 EB 0B 0F 2F C8 "
                                                       "76 06 4B 0F BA 2C D3 08 F3 0F 10 43 18 0F 2F C1 76 08 "
                                                       "4B 0F BA 2C D3 09 EB 0B 0F 2F C8 76 06 4B 0F BA 2C D3 0A";
    // Pod look feed hooks (method from the earlier CE VR mod's UpdatePitchYaw): the
    // builder's entry (+0x114C38; it contains kOdstActionTestBuilderSig at
    // +0x2A) and the two readers every look_relative evaluate calls - the
    // single-bit test (+0x1148D0, up/down/left/right pass 7..10) and the range
    // test (+0x114918, all_directions passes 7..10). Both readers load the
    // same TLS index as the builder.
    constinit const char* kOdstActionTestBuilderEntrySig =
        "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 56 "
        "48 83 EC 20 65 48 8B 04 25 58 00 00 00 49 8B D8 48 63 F1 48 8B FA "
        "8B 0D ?? ?? ?? ?? BD 01 00 00 00 41 BB D8 00 00 00 4C 8D 14 76 "
        "4C 8B 0C C8 4D 03 D2";
    constinit const char* kOdstActionTestBitReaderSig = "44 8B 0D ?? ?? ?? ?? 32 C0 65 4C 8B 14 25 58 00 00 00 33 D2 "
                                                        "44 8B C1 41 BB 60 04 00 00";
    constinit const char* kOdstActionTestRangeReaderSig =
        "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 8B 1D ?? ?? ?? ?? "
        "2B D1 65 48 8B 3C 25 58 00 00 00 41 BA 40 00 00 00";
    // Each look_relative evaluate: E8 at +0x12 (+0x15 for all_directions)
    // must land on the reader above.
    constinit const char* kOdstLookEvaluateSigs[] = {"40 53 48 83 EC 20 83 64 24 48 00 B9 07 00 00 00 8B DA E8",
        "40 53 48 83 EC 20 83 64 24 48 00 B9 08 00 00 00 8B DA E8",
        "40 53 48 83 EC 20 83 64 24 48 00 B9 09 00 00 00 8B DA E8",
        "40 53 48 83 EC 20 83 64 24 48 00 B9 0A 00 00 00 8B DA E8",
        "40 53 48 83 EC 20 83 64 24 48 00 8B DA BA 0A 00 00 00 8D 4A FD E8"};
    constexpr size_t kOdstActionTestTlsMember = 0xD8;
    constexpr size_t kOdstActionTestUserStride = 0x30;

    constinit const char* kOdstPlayerUnitGetterSig =
        "83 C8 FF 3B C8 74 27 8B 15 ?? ?? ?? ?? 65 48 8B 04 25 58 00 00 00 "
        "41 B8 08 01 00 00 48 63 C9 48 8B 04 D0 4A 8B 04 00 "
        "8B 84 88 C8 00 00 00 C3";
    // vehicle_definition_get_type (+0x3DB46C): cx = vehi tag index, returns
    // the first non-empty physics block 0..9 or 0xB when none is authored.
    // The two rip-relative globals are decoded from the matched body.
    constinit const char* kOdstVehicleTypeAccessorSig =
        "48 8B 05 ?? ?? ?? ?? 0F B7 C9 8B 54 C8 04 85 D2 75 04 33 D2 EB 0B "
        "48 8B 05 ?? ?? ?? ?? 48 8D 14 90 33 C0 45 33 C0 "
        "48 81 C2 E0 02 00 00 B9 0B 00 00 00 83 3A 00";
    // ODST's interpolated node-bank provider (+0x1B3938) and internal marker
    // resolver (+0x37F514). Same ABIs and body sizes as their Halo 3
    // homologs; both patterns also match once in halo3.dll, so the ODST
    // binding additionally requires the module identity the camera core
    // already pinned before it uses them.
    constinit const char* kOdstInterpolatedNodesSig = "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 "
                                                      "57 41 54 41 55 41 56 41 57 48 83 EC 30 "
                                                      "4C 8B EA 0F 29 70 C8 48 8D 50 20 8B F9 4D 8B F0";
    // ODST's eye-adaptation integrator tests this byte before it writes the
    // blended exposure; nonzero freezes adaptation. It is the engine's own
    // `render_exposure_lock` (named in HREK's debug menu and present in
    // H3ODSTEK's unstripped debug-var table), and it is read at exactly one
    // place in the whole module. The debug-var record ships with a NULL value
    // pointer in retail, so the storage is decoded from this instruction
    // instead: lock byte = matchRva + 7 + disp32 at match+3.
    constinit const char* kOdstExposureLockSig = "40 38 2D ?? ?? ?? ?? 0F 57 C9 0F 28 C4 F3 0F 5C C6 F3 0F 5A C8";
    // The byte kOdstExposureLockSig finds is the engine's INTERNAL copy of the
    // post-process settings block, and the engine rebuilds that copy from a
    // second, editable block once per rendered frame. `accept_edited_settings`
    // is inlined into main_render as this 0x38-byte global-to-global copy, and
    // it runs AFTER the camera-copy pass where we write and BEFORE the only
    // instruction that reads the lock. So writing the internal copy alone is
    // erased every frame before anyone looks at it -- measured: the byte held
    // exactly as designed for nine seat entries and the exposure still moved.
    // Matching the copy itself yields BOTH blocks, so we can write the source
    // the engine propagates from as well as the destination it lands in.
    //   editableBase = hit + 7  + disp32 at hit+3   (expected +0x2D7E210)
    //   internalBase = hit + 14 + disp32 at hit+10  (expected +0x8F0D40)
    //   *Lock        = *Base + 0x30 (m_auto_exposure_lock)
    // The internal lock this yields must equal the address the independent
    // kOdstExposureLockSig decode produces; two signatures agreeing on one
    // field is what replaces a hardcoded address here.
    constinit const char* kOdstPostprocessAcceptSig = "0F 10 05 ?? ?? ?? ?? 0F 11 05 ?? ?? ?? ?? 0F 10 0D ?? ?? ?? ?? "
                                                      "0F 11 0D ?? ?? ?? ?? 0F 10 05 ?? ?? ?? ?? 0F 11 05 ?? ?? ?? ?? "
                                                      "F2 0F 10 0D ?? ?? ?? ?? F2 0F 11 0D ?? ?? ?? ??";

    // The occupant camera-position evaluator (`unit_get_camera_position`) and
    // the weapon-barrel firing-data function that calls it. Both patterns also
    // match exactly once in halo3.dll, so they are only meaningful because the
    // camera core already pinned this module -- the scan MUST stay scoped to
    // the ODST base/size. A process-wide scan would land on Halo 3's own
    // headset-accepted first-person vehicle camera.
    constinit const char* kOdstUnitCameraPositionSig =
        "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 41 54 41 55 41 56 "
        "41 57 48 8D 68 A1 48 81 EC C0 00 00 00 44 8B 0D";
    constinit const char* kOdstWeaponBarrelFiringSig =
        "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 41 56 41 57 48 8D "
        "68 C1 48 81 EC C0 00 00 00 44 8B 15";

    constinit const char* kOdstMarkersInternalSig = "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 "
                                                    "66 44 89 48 20 57 41 54 41 55 41 56 41 57 48 83 EC 70 "
                                                    "45 33 F6 8B E9 49 8B F8 44 8B FA 8B D9 45 0F B7 DE";

    constinit const CameraRuntimeProfile kOdstCameraProfile = {
        L"halo3odst.dll",
        "Halo 3: ODST private camera core",
        0x68A0F232,
        0x4797000,
        {
            0x90,
            0xC0,
            // clang-format off
            0x00, 0x28, 0x34, 0x5C, 0x60, 0x68, 0x6C,
            0x00, 0x0C, 0x18, 0x24, 0x30,
            // clang-format on
            0x008,
            0x098,
            0x158,
            0x1E8,
            0x6C8,
            0x6D0,
            0x760,
            0x820,
            0x8B0,
            0x970,
            // clang-format off
            0x38, 0x4C, 0x5C, 0x28, 0x2C, 0x34, 0x64, 0x68,
            0x6C, 0x7C, 0x80, 0x78,
            // clang-format on
            0x27E0,
            0x27F0,
            0x27F4,
            0x27F8,
            0x27FC,
            0x2800,
            0x2804,
            0x2808,
            0x280C,
            0x2810,
        },
        "48 89 5C 24 08 57 48 83 EC 30 0F 29 74 24 20 48 8B FA 48 8B D9 48 85 D2 0F 84 ?? ?? ?? ?? F3 0F 10 15 ?? ?? ?? ?? B1 01 F3 0F 59 15 ?? ?? ?? ?? F3 0F 5E 15 ?? ?? ?? ??",
        "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 41 54 41 56 41 57 48 83 EC 40 8B 3D ?? ?? ?? ?? 48 8B F1 85 FF 0F 84 ?? ?? ?? ?? 8B 05 ?? ?? ?? ?? BA 00 00 00 00 0F BA E0 0A",
        "48 89 5C 24 08 57 48 83 EC 20 83 3D ?? ?? ?? ?? 03 8B FA 48 8B D9 48 89 0D ?? ?? ?? ??",
        "40 53 48 83 EC 30 44 0F BF 49 62 4C 8B D9 4C 8B 41 38 48 8B DA 0F BF 51 50",
        "48 8B C4 48 89 58 08 48 89 78 10 55 48 8D 68 E8 48 81 EC 10 01 00 00 80 3D ?? ?? ?? ?? 00",
        "48 8B C4 48 89 58 10 48 89 70 18 57 48 83 EC 60 48 8D 79 08 0F 29 70 E8 F3 0F 10 35 ?? ?? ?? ?? 48 8B D9 0F 29 78 D8 40 8A F2 48 8B 81 A8 02 00 00 B9 80 00 00 00 41 BA 58 01 00 00",
        "48 8B C4 48 89 58 08 55 48 8D 68 A1 48 81 EC C0 00 00 00 0F 29 70 E8 4C 8D 45 F7 0F 29 78 D8 48 8B D9 48 8B C2 48 83 C2 78 48 8B C8 E8 ?? ?? ?? ?? 48 8D 55 F7",
        "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 20 48 8B D9 40 8A F2 8B 89 FC 27 00 00 E8 ?? ?? ?? ?? 66 83 F8 FF 0F 85 ?? ?? ?? ?? B9 03 00 00 00",
        "39 35 ?? ?? ?? ?? 75 ?? 33 D2 48 8B CF E8 ?? ?? ?? ?? 40 38 35 ?? ?? ?? ?? 75 ?? 40 38 35 ?? ?? ?? ??",
        "48 89 5C 24 08 57 48 83 EC 20 48 8D 1D ?? ?? ?? ?? BF 04 00 00 00 48 8B CB E8 ?? ?? ?? ?? 48 81 C3 10 28 00 00 48 83 EF 01 75 ?? 48 8B 5C 24 30",
        "E8 ?? ?? ?? ?? 84 C0 74 ?? B9 03 00 00 00 E8 ?? ?? ?? ?? 84 C0 75 ?? 8B D1 B1 01 E8 ?? ?? ?? ?? C6 05 ?? ?? ?? ?? 01",
    };

    static_assert(sizeof(g_odstCamera.eyeCompactCamera) == 0x90);
    static_assert(sizeof(g_odstCamera.eyeDerivedBlock) == 0xC0);
    enum class OdstInstallResult
    {
        Installed,
        CleanupPending,
        NotReady,
        Failed,
    };

    struct OdstResolvedCameraRuntime
    {
        uintptr_t camCopy = 0;
        uintptr_t observerCameraEffect = 0;
        uintptr_t renderView = 0;
        uintptr_t prepareView = 0;
        uintptr_t buildViewport = 0;
        uintptr_t buildMatrices = 0;
        uintptr_t fpCameraRebuild = 0;
        uintptr_t fpCameraUpload = 0;
        uintptr_t fpDriver = 0;
        uintptr_t fpInterpolate = 0;
        uintptr_t fpVisiblePalette = 0;
        uintptr_t nativeWeaponIkDecision = 0;
        uintptr_t fpDriverGuard = 0;
        uintptr_t gunCameraConstructor = 0;
        uintptr_t nativePauseOwner = 0;
        uintptr_t hudPhasePrimary = 0;
        uintptr_t hudPhaseSecondary = 0;
        uintptr_t hudTargetCopy = 0;
        uint8_t* nativePauseFlag = nullptr;
        float* motionBlurScale = nullptr;
        float* motionBlurMax = nullptr;
        uintptr_t gunCameraArray = 0;
    };

    struct OdstStaticPreflightCache
    {
        bool valid = false;
        uintptr_t moduleBase = 0;
        size_t moduleSize = 0;
        OdstResolvedCameraRuntime resolved{};
    };

    constinit OdstStaticPreflightCache g_odstPreflightCache;

    void ClearOdstStaticPreflightCache()
    {
        g_odstPreflightCache = {};
    }

    bool ApplyOdstNativeWeaponIkBypass(uintptr_t decision)
    {
        uint8_t* branch = reinterpret_cast<uint8_t*>(decision + 3);
        if (!decision || branch[0] != 0x74 || branch[1] != 0x05)
        {
            LOG("ODST VRIK: native weapon-IK branch verification failed");
            return false;
        }
        g_odstCamera.nativeWeaponIkBranch = branch;
        DWORD oldProtect = 0;
        if (!VirtualProtect(branch, 2, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            LOG("ODST VRIK: native weapon-IK branch protection failed");
            return false;
        }
        branch[0] = 0xEB;
        branch[1] = 0x18;
        g_odstCamera.nativeWeaponIkPatched = true;
        FlushInstructionCache(GetCurrentProcess(), branch, 2);
        DWORD ignored = 0;
        if (!VirtualProtect(branch, 2, oldProtect, &ignored))
        {
            LOG("ODST VRIK: could not restore weapon-IK page protection");
            return false;
        }
        LOG("ODST VRIK: native support-hand IK bypassed at "
            "halo3odst.dll+0x%llX; shared controller solver owns both arms",
            static_cast<unsigned long long>(decision - g_odstCamera.moduleBase));
        return true;
    }

    bool RestoreOdstNativeWeaponIkBypass()
    {
        uint8_t* branch = g_odstCamera.nativeWeaponIkBranch;
        if (!branch)
            return !g_odstCamera.nativeWeaponIkPatched;
        if (branch[0] == 0x74 && branch[1] == 0x05)
        {
            g_odstCamera.nativeWeaponIkBranch = nullptr;
            g_odstCamera.nativeWeaponIkPatched = false;
            return true;
        }
        if (branch[0] != 0xEB || branch[1] != 0x18)
        {
            LOG("ODST VRIK cleanup: weapon-IK branch has unknown bytes");
            return false;
        }
        DWORD oldProtect = 0;
        if (!VirtualProtect(branch, 2, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            LOG("ODST VRIK cleanup: branch protection failed");
            return false;
        }
        branch[0] = 0x74;
        branch[1] = 0x05;
        FlushInstructionCache(GetCurrentProcess(), branch, 2);
        DWORD ignored = 0;
        if (!VirtualProtect(branch, 2, oldProtect, &ignored))
        {
            LOG("ODST VRIK cleanup: page protection restore failed");
            return false;
        }
        g_odstCamera.nativeWeaponIkBranch = nullptr;
        g_odstCamera.nativeWeaponIkPatched = false;
        return true;
    }

    bool RestoreOdstCrosshairClassGate()
    {
        uint8_t* gate = g_odstCamera.crosshairClassGate;
        if (!gate)
            return !g_odstCamera.crosshairClassGatePatched;
        const bool alreadyStock = gate[0] == 0x74 && gate[1] == 0x17;
        if (!alreadyStock && (gate[0] != 0x90 || gate[1] != 0x90))
        {
            LOG("ODST crosshair cleanup: class-gate has unknown bytes");
            return false;
        }

        DWORD currentProtect = 0;
        if (!VirtualProtect(gate, 2, PAGE_EXECUTE_READWRITE, &currentProtect))
        {
            LOG("ODST crosshair cleanup: class-gate protection failed");
            return false;
        }
        if (!alreadyStock)
        {
            gate[0] = 0x74;
            gate[1] = 0x17;
            FlushInstructionCache(GetCurrentProcess(), gate, 2);
        }

        // If install patched the bytes but failed to restore their protection,
        // currentProtect is RWX. Retain the original protection explicitly so
        // cleanup still restores the page to its pre-install state.
        const DWORD targetProtect = g_odstCamera.crosshairClassGateOriginalProtect
                                        ? g_odstCamera.crosshairClassGateOriginalProtect
                                        : currentProtect;
        DWORD ignored = 0;
        if (!VirtualProtect(gate, 2, targetProtect, &ignored))
        {
            LOG("ODST crosshair cleanup: could not restore class-gate protection");
            return false;
        }
        g_odstCamera.crosshairClassGate = nullptr;
        g_odstCamera.crosshairClassGatePatched = false;
        g_odstCamera.crosshairClassGateOriginalProtect = 0;
        return true;
    }

    enum class OdstOptionalHookResult
    {
        StockFallback,
        Installed,
        CleanupRequired,
    };

    // ODST HUD height. Retail ODST and H3ODSTEK independently prove this as
    // chud_compute_anchor_basis: the ABI matches Halo 3 and its final
    // real_matrix4x3 translation writes Y at basis+0x2C. The long entry AOB
    // captures all four arguments; the unique tail AOB independently guards
    // the exact +0x932 output sequence in the supported retail function.
    // The shared tail of an ODST optional hook once its native proof passed:
    // refuse when the lifecycle registry is full, create (stock fallback on
    // failure), register target + trampoline, then enable. A failed enable keeps
    // the registered target, trampoline and title module and asks for the whole
    // transaction's verified cleanup, even if MinHook partially enabled.
    // createFailed/enableFailed are printf formats given the MinHook status.
    OdstOptionalHookResult InstallOdstOptionalHook(uintptr_t target, void* detour, void** original,
        const char* capacityExhausted, const char* createFailed, const char* enableFailed)
    {
        if (g_odstCamera.hooks.Full())
        {
            LOG("%s", capacityExhausted);
            return OdstOptionalHookResult::StockFallback;
        }
        void* const address = reinterpret_cast<void*>(target);
        const MH_STATUS createStatus = MCCVR_CreateHookChecked(address, detour, original);
        if (createStatus != MH_OK)
        {
            *original = nullptr;
            LOG(createFailed, static_cast<int>(createStatus));
            return OdstOptionalHookResult::StockFallback;
        }
        g_odstCamera.hooks.Remember(address, *original);
        const MH_STATUS enableStatus = MH_EnableHook(address);
        if (enableStatus != MH_OK)
        {
            LOG(enableFailed, static_cast<int>(enableStatus));
            return OdstOptionalHookResult::CleanupRequired;
        }
        return OdstOptionalHookResult::Installed;
    }

    OdstOptionalHookResult InstallOdstHudHeight(uintptr_t base, size_t size)
    {
        const char* kOdstHudAnchorBasisSig = "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 20 55 "
                                             "41 54 41 55 41 56 41 57 48 8D 68 98 48 81 EC 40 01 00 00 "
                                             "4C 8B 2D ?? ?? ?? ?? 49 8B F9 0F 29 70 C8 41 8B D8 "
                                             "0F 29 78 B8 48 8B F2 44 0F 29 40 A8 44 0F 29 48 98 "
                                             "48 8B 05 ?? ?? ?? ?? 4C 63 E1";
        const char* kOdstHudAnchorBasisTailSig = "F2 0F 10 44 24 38 8B 54 24 54 F2 0F 11 7F 1C "
                                                 "89 4F 24 8B 4C 24 40 F2 0F 11 47 28 "
                                                 "F2 44 0F 11 4F 04 F2 44 0F 11 47 10 "
                                                 "89 4F 30 C7 07 00 00 80 3F";

        const uintptr_t anchorBasis = sig::Find(base, size, kOdstHudAnchorBasisSig);
        const bool uniqueEntry =
            anchorBasis && !sig::Find(anchorBasis + 1, base + size - anchorBasis - 1, kOdstHudAnchorBasisSig);
        const uintptr_t outputTail = sig::Find(base, size, kOdstHudAnchorBasisTailSig);
        const bool uniqueTail =
            outputTail && !sig::Find(outputTail + 1, base + size - outputTail - 1, kOdstHudAnchorBasisTailSig);
        const bool exactSupportedLayout =
            uniqueEntry && uniqueTail && outputTail > anchorBasis && outputTail - anchorBasis == 0x932;
        if (!uniqueEntry || !exactSupportedLayout)
        {
            LOG("ODST HUD height: anchor-basis entry/output proof missing or "
                "ambiguous; height remains stock");
            return OdstOptionalHookResult::StockFallback;
        }
        const OdstOptionalHookResult hooked = InstallOdstOptionalHook(anchorBasis,
            reinterpret_cast<void*>(&OdstHudAnchorBasisHook), &g_odstCamera.originalHudAnchorBasis,
            "ODST HUD height: lifecycle hook capacity exhausted; height remains stock",
            "ODST HUD height: hook create failed; height remains stock",
            "ODST HUD height: hook enable failed (%d); requesting verified "
            "transaction cleanup");
        if (hooked != OdstOptionalHookResult::Installed)
            return hooked;

        LOG("ODST HUD height: title-native anchor hook active at "
            "halo3odst.dll+0x%llX (basis Y +0x2C)",
            static_cast<unsigned long long>(anchorBasis - base));
        return OdstOptionalHookResult::Installed;
    }

    // ODST game brightness. `kHudXformSig` is the same AOB Halo 3 uses and
    // it is proven to match exactly once in
    // halo3odst.dll, at +0x2A6308, with the identical input shuffle, the same
    // powf call and the same four constant uploads (0x280000, 0x2D0000,
    // 0x280001, 0x2D0001). ODST simply omits Halo 3's extra eight-byte upload
    // to 0x082F0005, which carries none of the colour maths. Missing or
    // ambiguous proof leaves ODST at its stock brightness.
    OdstOptionalHookResult InstallOdstBrightness(uintptr_t base, size_t size)
    {
        const char* kOdstHudXformSig = "40 55 48 8B EC 48 83 EC 50 0F 29 74 24 40 0F 28 F1 "
                                       "0F 29 7C 24 30 0F 28 CA 0F 28 F8";
        const uintptr_t hudXform = sig::Find(base, size, kOdstHudXformSig);
        if (!hudXform || sig::Find(hudXform + 1, base + size - hudXform - 1, kOdstHudXformSig))
        {
            LOG("ODST brightness: screen colour/gamma signature missing or "
                "ambiguous; brightness stays at the game's own");
            return OdstOptionalHookResult::StockFallback;
        }
        const OdstOptionalHookResult hooked =
            InstallOdstOptionalHook(hudXform, reinterpret_cast<void*>(&OdstHudXformHook), &g_odstOriginalHudXform,
                "ODST brightness: lifecycle hook capacity exhausted; "
                "brightness stays at the game's own",
                "ODST brightness: hook create failed (%d); brightness stays at "
                "the game's own",
                "ODST brightness: hook enable failed (%d); requesting verified "
                "transaction cleanup");
        if (hooked != OdstOptionalHookResult::Installed)
            return hooked;

        LOG("ODST brightness: hook active at halo3odst.dll+0x%llX "
            "(game_brightness)",
            static_cast<unsigned long long>(hudXform - base));
        return OdstOptionalHookResult::Installed;
    }

    // ---- Pod look feed (method from the earlier CE VR mod's UpdatePitchYaw hook) ----
    // the earlier CE VR mod feeds (head - facing) as the native look delta so the game's
    // own action-test builder sees real look input. Here only the builder
    // sees the head: for its call alone the input's facing deltas carry the
    // direction the head just turned (head_look_test::Step, 5 degrees), then
    // the real values are restored - facing, view and aim never change. Fed
    // only in the drop pod, for user 0, and only the directions a script is
    // polling (head_look_test::FeedBits).
    using OdstLookBuilderFn = void(__fastcall*)(int, void*, unsigned char*);
    using OdstLookBitFn = bool(__fastcall*)(int);
    using OdstLookRangeFn = bool(__fastcall*)(int, int);
    constinit OdstLookBuilderFn g_odstOriginalLookBuilder = nullptr;
    constinit OdstLookBitFn g_odstOriginalLookBit = nullptr;
    constinit OdstLookRangeFn g_odstOriginalLookRange = nullptr;

    __declspec(noinline) bool __fastcall OdstLookBitHook(int bit)
    {
        g_odstCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        bool result = false;
        __try
        {
            if (bit >= 7 && bit <= 10)
                g_odstLookPolls.Note(static_cast<head_look_test::Poll>(bit - 7), GetTickCount64());
            if (g_odstOriginalLookBit)
                result = g_odstOriginalLookBit(bit);
        }
        __finally
        {
            g_odstCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
        return result;
    }

    __declspec(noinline) bool __fastcall OdstLookRangeHook(int first, int last)
    {
        g_odstCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        bool result = false;
        __try
        {
            if (first == 7 && last == 10)
                g_odstLookPolls.Note(head_look_test::PollAll, GetTickCount64());
            if (g_odstOriginalLookRange)
                result = g_odstOriginalLookRange(first, last);
        }
        __finally
        {
            g_odstCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
        return result;
    }

    // The look bits to feed this builder call (0 = leave the input alone).
    uint64_t OdstPodLookBitsToFeed()
    {
        static head_look_test::Anchor anchor;
        const uint64_t now = GetTickCount64();
        const uint64_t podAt = g_odstDropPodSeatMs.load(std::memory_order_acquire);
        const head_look_test::PollSnapshot polls = head_look_test::Snapshot(g_odstLookPolls, now);
        bool anyPoll = false;
        for (bool polled : polls.polled)
            anyPoll |= polled;
        float q[4], p[3];
        if (!anyPoll || !g_config.odst_pod_head_look || !podAt || now < podAt || now - podAt > 500 ||
            !g_odstEngineTlsIndex || !VR_GetHeadPose(q, p))
        {
            anchor = {};
            return 0;
        }
        float yaw = 0.0f, pitch = 0.0f;
        head_look_test::YawPitchFromQuat(q, yaw, pitch);
        const uint64_t counted = head_look_test::Step(anchor, yaw, pitch);
        if (!counted)
            return 0;
        uint64_t game = 0;
        __try
        {
            auto** slots = reinterpret_cast<void**>(__readgsqword(0x58));
            auto* tls = slots ? reinterpret_cast<unsigned char*>(slots[*g_odstEngineTlsIndex]) : nullptr;
            auto* users = tls ? *reinterpret_cast<unsigned char**>(tls + kOdstActionTestTlsMember) : nullptr;
            if (!users)
                return 0;
            game = *reinterpret_cast<volatile uint64_t*>(users);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading the ODST pod look bits faulted");
            return 0;
        }
        return head_look_test::FeedBits(polls, counted, game);
    }

    void OdstLookBuilderBody(int user, void* context, unsigned char* input)
    {
        if (!g_odstOriginalLookBuilder)
            return;
        const uint64_t feed = user == 0 && input ? OdstPodLookBitsToFeed() : 0;
        if (!feed)
        {
            g_odstOriginalLookBuilder(user, context, input);
            return;
        }
        // s_player_control_input: +0x18 yaw delta (> 0 = left), +0x1C pitch
        // delta (> 0 = up) - the builder's own reads at +0x114FD0.
        float* yaw = reinterpret_cast<float*>(input + 0x18);
        float* pitch = reinterpret_cast<float*>(input + 0x1C);
        const float savedYaw = *yaw, savedPitch = *pitch;
        // The builder clamps these against the look-inhibit flags (+0x115168);
        // feed idle axes only, so restoring them is exact.
        const uint64_t idleFeed = head_look_test::OnlyIdleAxes(feed, savedYaw, savedPitch);
        if (!idleFeed)
        {
            g_odstOriginalLookBuilder(user, context, input);
            return;
        }
        const uint64_t feedBits = idleFeed;
        const float step = head_look_test::kStepRadians;
        if (feedBits & head_look_test::kLeftBit)
            *yaw = step;
        else if (feedBits & head_look_test::kRightBit)
            *yaw = -step;
        if (feedBits & head_look_test::kUpBit)
            *pitch = step;
        else if (feedBits & head_look_test::kDownBit)
            *pitch = -step;
        __try
        {
            g_odstOriginalLookBuilder(user, context, input);
        }
        __finally
        {
            *yaw = savedYaw;
            *pitch = savedPitch;
        }
        g_odstPodLookFedBits.fetch_or(feedBits, std::memory_order_relaxed);
        g_odstPodLookFedTicks.fetch_add(1, std::memory_order_relaxed);
    }

    __declspec(noinline) void __fastcall OdstLookBuilderHook(int user, void* context, unsigned char* input)
    {
        g_odstCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            OdstLookBuilderBody(user, context, input);
        }
        __finally
        {
            g_odstCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    // Optional: any missing proof leaves the pod's look step stick-only.
    OdstOptionalHookResult InstallOdstPodLookFeed(uintptr_t base, size_t size)
    {
        g_odstPodLookFeedReady.store(false, std::memory_order_release);
        auto unique = [&](const char* pattern) -> uintptr_t
        {
            const uintptr_t hit = sig::Find(base, size, pattern);
            return hit && !sig::Find(hit + 1, base + size - hit - 1, pattern) ? hit : 0;
        };
        const uintptr_t builder = unique(kOdstActionTestBuilderEntrySig);
        const uintptr_t bitReader = unique(kOdstActionTestBitReaderSig);
        const uintptr_t rangeReader = unique(kOdstActionTestRangeReaderSig);
        auto tlsOf = [](uintptr_t instruction)
        { return instruction + 6 + *reinterpret_cast<const int32_t*>(instruction + 2); };
        // "8B 0D d32" at builder+0x2A, "44 8B 0D d32" at bitReader,
        // "8B 1D d32" at rangeReader+0x0F: all one TLS index.
        bool proven = builder && bitReader && rangeReader && tlsOf(builder + 0x2A) == tlsOf(bitReader + 1) &&
                      tlsOf(builder + 0x2A) == tlsOf(rangeReader + 0x0F) && g_odstEngineTlsIndex &&
                      tlsOf(builder + 0x2A) == reinterpret_cast<uintptr_t>(g_odstEngineTlsIndex);
        for (int i = 0; proven && i < 5; ++i)
        {
            const uintptr_t evaluate = unique(kOdstLookEvaluateSigs[i]);
            const size_t call = i < 4 ? 0x12 : 0x15;
            proven = evaluate &&
                     sig::RipTarget(evaluate + call + 1, evaluate + call + 5) == (i < 4 ? bitReader : rangeReader);
        }
        if (!proven)
        {
            LOG("ODST pod look feed: builder/reader/evaluate proof missing or "
                "ambiguous (builder=+0x%llX bit=+0x%llX range=+0x%llX); the "
                "pod's look step stays stick-only",
                (unsigned long long)(builder ? builder - base : 0),
                (unsigned long long)(bitReader ? bitReader - base : 0),
                (unsigned long long)(rangeReader ? rangeReader - base : 0));
            return OdstOptionalHookResult::StockFallback;
        }
        struct Spec
        {
            uintptr_t target;
            void* detour;
            void** original;
        };
        const Spec specs[] = {
            {bitReader, reinterpret_cast<void*>(&OdstLookBitHook), reinterpret_cast<void**>(&g_odstOriginalLookBit)},
            {rangeReader, reinterpret_cast<void*>(&OdstLookRangeHook),
                reinterpret_cast<void**>(&g_odstOriginalLookRange)},
            {builder, reinterpret_cast<void*>(&OdstLookBuilderHook),
                reinterpret_cast<void**>(&g_odstOriginalLookBuilder)}};
        for (const Spec& spec : specs)
        {
            const OdstOptionalHookResult hooked = InstallOdstOptionalHook(spec.target, spec.detour, spec.original,
                "ODST pod look feed: hook capacity exhausted; the pod's "
                "look step stays stick-only",
                "ODST pod look feed: hook create failed (%d); the pod's "
                "look step stays stick-only",
                "ODST pod look feed: hook enable failed (%d); requesting "
                "verified transaction cleanup");
            // Hooks already in place only observe/forward; the builder is
            // last, so no feed runs unless all three are live.
            if (hooked != OdstOptionalHookResult::Installed)
                return hooked;
        }
        g_odstPodLookFeedReady.store(true, std::memory_order_release);
        LOG("ODST pod look feed: builder +0x%llX and look-test readers "
            "+0x%llX/+0x%llX hooked; while a script polls a look test in the "
            "pod, head look reaches the builder as look input",
            (unsigned long long)(builder - base), (unsigned long long)(bitReader - base),
            (unsigned long long)(rangeReader - base));
        return OdstOptionalHookResult::Installed;
    }

    // ODST class-2 CHUD crosshair hider + authored-widget capture. Full parity
    // with Halo 3's InstallHalo3CameraHooks crosshair path, but every location is ODST-proven
    // (the kHudElemSig candidate; disassembly at
    // halo3odst.dll+0x329488). The class-gate block is byte-identical to Halo 3
    // yet shifted -3 bytes inside the recompiled function: playback call E8 at
    // +0x7A, playback short-circuit je (74 17) at +0x81, class-2 predicate call
    // E8 at +0x91. Missing proof fails open to ODST's stock native crosshair.
    OdstOptionalHookResult InstallOdstCrosshairHider(uintptr_t base, size_t size)
    {
        const char* kHudElemSigOdst = "44 88 4C 24 20 53 48 83 EC 50 48 8B 05 ?? ?? ?? ?? 8B D9 45 0F B7 "
                                      "C0 89 4C 24 20 48 8B 0D ?? ?? ?? ?? 48 89 54 24 28 46 8B 4C C0 04 "
                                      "45 85 C9 75 ?? 33 C0 EB ??";
        uintptr_t hudElem = sig::Find(base, size, kHudElemSigOdst);
        if (!hudElem || sig::Find(hudElem + 1, base + size - hudElem - 1, kHudElemSigOdst))
        {
            LOG("ODST crosshair: chud_draw_widget signature missing/ambiguous; "
                "native crosshair stays visible");
            return OdstOptionalHookResult::StockFallback;
        }

        // clang-format off
        const unsigned char expectedClassGate[] = {
            0x74, 0x17, 0xB8, 0x02, 0x00, 0x00, 0x00,
            0x66, 0x41, 0x3B, 0x42, 0x04, 0x75, 0x0B,
            0x8B, 0xCB, 0xE8
            // clang-format on
        };
        unsigned char* fn = reinterpret_cast<unsigned char*>(hudElem);
        unsigned char* classGate = fn + 0x81;
        if (fn[0x7A] != 0xE8 || memcmp(classGate, expectedClassGate, sizeof(expectedClassGate)) != 0)
        {
            LOG("ODST crosshair: class-gate layout mismatch; native crosshair "
                "stays visible");
            return OdstOptionalHookResult::StockFallback;
        }

        const uintptr_t playbackTarget = sig::RipTarget(hudElem + 0x7B, hudElem + 0x7F);
        const uintptr_t visibleTarget = sig::RipTarget(hudElem + 0x92, hudElem + 0x96);
        if (playbackTarget < base || playbackTarget >= base + size || visibleTarget < base ||
            visibleTarget >= base + size)
        {
            LOG("ODST crosshair: class targets outside halo3odst.dll; native "
                "crosshair stays visible");
            return OdstOptionalHookResult::StockFallback;
        }
        if (g_odstCamera.hooks.Count() + 2 > g_odstCamera.hooks.kCapacity)
        {
            LOG("ODST crosshair: lifecycle hook capacity exhausted; native "
                "crosshair stays visible");
            return OdstOptionalHookResult::StockFallback;
        }

        const MH_STATUS visibleCreate = MCCVR_CreateHookChecked(reinterpret_cast<void*>(visibleTarget),
            reinterpret_cast<void*>(&OdstHudCrosshairVisibleHook),
            reinterpret_cast<void**>(&g_realHudCrosshairVisible));
        if (visibleCreate != MH_OK)
        {
            g_realHudCrosshairVisible = nullptr;
            LOG("ODST crosshair: visibility predicate hook create failed; "
                "native crosshair stays visible");
            return OdstOptionalHookResult::StockFallback;
        }
        g_odstCamera.hooks.Remember(
            reinterpret_cast<void*>(visibleTarget), reinterpret_cast<void*>(g_realHudCrosshairVisible));

        const MH_STATUS drawCreate = MCCVR_CreateHookChecked(reinterpret_cast<void*>(hudElem),
            reinterpret_cast<void*>(&OdstHudDrawWidgetHook), reinterpret_cast<void**>(&g_realHudDrawWidget));
        if (drawCreate != MH_OK)
        {
            const MH_STATUS removeStatus = MCCVR_RemoveHookChecked(reinterpret_cast<void*>(visibleTarget));
            if (removeStatus == MH_OK || removeStatus == MH_ERROR_NOT_CREATED)
            {
                g_odstCamera.hooks.Forget(reinterpret_cast<void*>(visibleTarget));
                g_realHudCrosshairVisible = nullptr;
                g_realHudDrawWidget = nullptr;
                LOG("ODST crosshair: widget hook create failed; native "
                    "crosshair stays visible");
                return OdstOptionalHookResult::StockFallback;
            }
            LOG("ODST crosshair: widget hook create failed and visibility "
                "cleanup failed (%d); requesting verified transaction cleanup",
                static_cast<int>(removeStatus));
            return OdstOptionalHookResult::CleanupRequired;
        }
        g_odstCamera.hooks.Remember(reinterpret_cast<void*>(hudElem), reinterpret_cast<void*>(g_realHudDrawWidget));

        DWORD oldProtect = 0;
        if (!VirtualProtect(classGate, 2, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            const MH_STATUS drawRemove = MCCVR_RemoveHookChecked(reinterpret_cast<void*>(hudElem));
            const MH_STATUS visibleRemove = MCCVR_RemoveHookChecked(reinterpret_cast<void*>(visibleTarget));
            const bool removed = (drawRemove == MH_OK || drawRemove == MH_ERROR_NOT_CREATED) &&
                                 (visibleRemove == MH_OK || visibleRemove == MH_ERROR_NOT_CREATED);
            if (removed)
            {
                g_odstCamera.hooks.Forget(reinterpret_cast<void*>(hudElem));
                g_odstCamera.hooks.Forget(reinterpret_cast<void*>(visibleTarget));
                g_realHudDrawWidget = nullptr;
                g_realHudCrosshairVisible = nullptr;
                LOG("ODST crosshair: class-gate protection failed; native "
                    "crosshair stays visible");
                return OdstOptionalHookResult::StockFallback;
            }
            LOG("ODST crosshair: class-gate protection and hook cleanup failed "
                "(%d/%d); requesting verified transaction cleanup",
                static_cast<int>(drawRemove), static_cast<int>(visibleRemove));
            return OdstOptionalHookResult::CleanupRequired;
        }

        classGate[0] = 0x90;
        classGate[1] = 0x90;
        FlushInstructionCache(GetCurrentProcess(), classGate, 2);
        g_odstCamera.crosshairClassGate = classGate;
        g_odstCamera.crosshairClassGatePatched = true;
        g_odstCamera.crosshairClassGateOriginalProtect = oldProtect;
        DWORD ignored = 0;
        if (!VirtualProtect(classGate, 2, oldProtect, &ignored))
        {
            LOG("ODST crosshair: class-gate page protection restore failed; "
                "requesting verified transaction cleanup");
            return OdstOptionalHookResult::CleanupRequired;
        }

        g_gameIsPlayback = reinterpret_cast<GameIsPlaybackFn>(playbackTarget);
        const MH_STATUS visibleQueue = MH_QueueEnableHook(reinterpret_cast<void*>(visibleTarget));
        const MH_STATUS drawQueue =
            visibleQueue == MH_OK ? MH_QueueEnableHook(reinterpret_cast<void*>(hudElem)) : MH_UNKNOWN;
        const MH_STATUS applyStatus = visibleQueue == MH_OK && drawQueue == MH_OK ? MH_ApplyQueued() : MH_UNKNOWN;
        if (visibleQueue != MH_OK || drawQueue != MH_OK || applyStatus != MH_OK)
        {
            // MH_ApplyQueued can enable its first target and then fail its
            // second. Preserve every target/trampoline/patch and use the same
            // verified quiesce path as normal title teardown.
            LOG("ODST crosshair: atomic hook enable failed (%d/%d/%d); "
                "requesting verified transaction cleanup",
                static_cast<int>(visibleQueue), static_cast<int>(drawQueue), static_cast<int>(applyStatus));
            return OdstOptionalHookResult::CleanupRequired;
        }

        LOG("ODST crosshair: authored CHUD crosshair redirect active at "
            "halo3odst.dll+0x%llX (class-2 predicate +0x%llX)",
            static_cast<unsigned long long>(hudElem - base), static_cast<unsigned long long>(visibleTarget - base));
        return OdstOptionalHookResult::Installed;
    }

    void ClearOdstCameraPointers()
    {
        g_odstCamera.moduleBase = 0;
        g_odstCamera.moduleSize = 0;
        g_odstCamera.moduleReference = nullptr;
        g_odstCamera.originalCamCopy = nullptr;
        g_odstCamera.originalObserverCameraEffect = nullptr;
        g_odstCamera.originalRenderView = nullptr;
        g_odstCamera.prepareView = nullptr;
        g_odstCamera.buildViewport = nullptr;
        g_odstCamera.buildMatrices = nullptr;
        g_odstCamera.originalFpCameraRebuild = nullptr;
        g_odstCamera.fpCameraUpload = nullptr;
        g_odstCamera.originalFpDriver = nullptr;
        g_odstCamera.originalHudPhasePrimary = nullptr;
        g_odstCamera.originalHudPhaseSecondary = nullptr;
        g_odstCamera.originalHudTargetCopy = nullptr;
        g_odstCamera.originalHudAnchorBasis = nullptr;
        g_odstOriginalHudXform = nullptr;
        g_odstCamera.originalFpInterpolate = nullptr;
        g_odstCamera.originalFpVisiblePalette = nullptr;
        g_odstCamera.nativeWeaponIkBranch = nullptr;
        g_odstCamera.nativeWeaponIkPatched = false;
        g_odstCamera.crosshairClassGate = nullptr;
        g_odstCamera.crosshairClassGatePatched = false;
        g_odstCamera.crosshairClassGateOriginalProtect = 0;
        // Drop the shared CHUD trampoline pointers so a later title change can
        // never call into an unloaded halo3odst.dll (stale-pointer crash class).
        g_realHudCrosshairVisible = nullptr;
        g_realHudDrawWidget = nullptr;
        g_gameIsPlayback = nullptr;
        g_odstCamera.gunCameraArray = 0;
        g_odstCamera.eyeView.store(nullptr, std::memory_order_release);
        memset(g_odstCamera.eyeCompactCamera, 0, sizeof(g_odstCamera.eyeCompactCamera));
        memset(g_odstCamera.eyeDerivedBlock, 0, sizeof(g_odstCamera.eyeDerivedBlock));
        for (OdstMotionBlurVar& var : g_odstCamera.motionBlurVars)
            var = {};
        g_odstCamera.motionBlurResolved = false;
        g_odstCamera.motionBlurZeroed = false;
        g_odstCamera.cinematicCameraTlsMemberOffset = 0;
        // Drop the cinematic-FOV debug-var pointer for the same stale-pointer
        // reason as the trampolines above: a later title change must never let
        // UpdateCinematicFovPolicy write into an unloaded halo3odst.dll. Halo 3
        // re-resolves its own pointer in InstallHalo3CameraHooks when it next owns the game.
        g_reduceCinematicFov.store(nullptr, std::memory_order_release);
        g_reduceCinematicFovApplied.store(false, std::memory_order_release);
        g_odstCamera.hooks = {};
        g_odstCamera.renderHookTarget = nullptr;
        g_odstWorldCollision.runtimeBoundsProven.store(false, std::memory_order_release);
        g_odstWorldCollision.installed.store(false, std::memory_order_release);
        g_odstWorldCollision.target = nullptr;
        g_odstWorldCollision.original = nullptr;
        g_odstWorldCollision.resolver = nullptr;
        g_odstCamera.installedAtMs.store(0, std::memory_order_release);
        g_odstCamera.cameraArrayReady.store(false, std::memory_order_release);
    }

    void LocateOdstCinematicUserInputState(uintptr_t base, size_t size)
    {
        // Optional, feature-local proof: failure masks only ODST theatre. The
        // camera core remains armed and every cinematic stays immersive.
        g_odstCamera.cinematicCameraTlsMemberOffset = 0;
        const uintptr_t implementation = sig::Find(base, size, kOdstCinematicUserInputConstraintSig);
        const uintptr_t updater = sig::Find(base, size, kOdstCinematicUserInputUpdateSig);
        const bool uniqueImplementation =
            implementation &&
            !sig::Find(implementation + 1, base + size - implementation - 1, kOdstCinematicUserInputConstraintSig);
        const bool uniqueUpdater =
            updater && !sig::Find(updater + 1, base + size - updater - 1, kOdstCinematicUserInputUpdateSig);
        if (!uniqueImplementation || !uniqueUpdater || implementation + 0x19 > base + size)
        {
            LOG("ODST cutscene theatre: user-input-constraint setter/updater "
                "signature missing/ambiguous; theatre capability disabled");
            return;
        }

        const int32_t tlsDisp = *reinterpret_cast<const int32_t*>(implementation + 3);
        auto* constraintTlsIndex = reinterpret_cast<uint32_t*>(implementation + 7 + tlsDisp);
        const uintptr_t constraintTlsIndexAddress = reinterpret_cast<uintptr_t>(constraintTlsIndex);
        const uint32_t cameraMemberOffset = *reinterpret_cast<const uint32_t*>(implementation + 0x15);
        if (!g_cinematicTlsIndex || constraintTlsIndex != g_cinematicTlsIndex || constraintTlsIndexAddress < base ||
            constraintTlsIndexAddress + sizeof(uint32_t) > base + size || *constraintTlsIndex >= 256 ||
            cameraMemberOffset < 0x40 || cameraMemberOffset > 0x400 ||
            (cameraMemberOffset & (sizeof(uintptr_t) - 1)) != 0)
        {
            LOG("ODST cutscene theatre: user-input-constraint TLS proof "
                "failed; theatre capability disabled");
            return;
        }

        g_odstCamera.cinematicCameraTlsMemberOffset = cameraMemberOffset;
        LOG("ODST cutscene theatre: live authored look constraints resolved "
            "(TLS member 0x%X, current C8/CC/D0/D4, rates D8/DC/E0/E4, "
            "ticks F0); only zero look freedom can enter theatre",
            cameraMemberOffset);
    }

    // Pod look-around proof (kOdstActionTest*Sig). Failure only leaves the
    // pod's look step to the stick, as stock.
    void ResolveOdstLookTestProof(uintptr_t base, size_t size, uintptr_t tlsAddress)
    {
        g_odstLookTestProven.store(false, std::memory_order_release);
        auto unique = [&](const char* pattern) -> uintptr_t
        {
            const uintptr_t hit = sig::Find(base, size, pattern);
            return hit && !sig::Find(hit + 1, base + size - hit - 1, pattern) ? hit : 0;
        };
        const uintptr_t reset = unique(kOdstActionTestResetSig);
        const uintptr_t builder = unique(kOdstActionTestBuilderSig);
        const uintptr_t look = unique(kOdstActionTestLookBitsSig);
        auto tlsOf = [](uintptr_t hit) { return hit + 6 + *reinterpret_cast<const int32_t*>(hit + 2); };
        const bool proven = reset && builder && look && tlsOf(reset) == tlsAddress && tlsOf(builder) == tlsAddress;
        g_odstLookTestProven.store(proven, std::memory_order_release);
        LOG("ODST pod look-around: action-test accumulator %s (reset=+0x%llX "
            "builder=+0x%llX look=+0x%llX, TLS %s); %s",
            proven ? "proven" : "NOT proven", (unsigned long long)(reset ? reset - base : 0),
            (unsigned long long)(builder ? builder - base : 0), (unsigned long long)(look ? look - base : 0),
            reset && builder && tlsOf(reset) == tlsAddress && tlsOf(builder) == tlsAddress ? "matches" : "mismatch",
            proven ? "head look counts as look input in the drop pod" : "the pod's look step stays stick-only (stock)");
    }

    // Drop pod: count head look as the look-around input the pod script tests
    // (head_look_test::Step) and log what the game saw. Camera thread, the game's
    // own TLS, exactly where OdstSampleVehicleState reads the object table.
    void OdstPodLookTick(bool podSeat, uint64_t nowMs)
    {
        // Diagnostic only (camera thread, the game's own TLS): what the game's
        // look tests hold, what the builder hook fed and the servo's stick.
        static uint64_t lastLogMs = 0;
        static uint64_t seen = 0;
        if (!podSeat || !g_odstLookTestProven.load(std::memory_order_acquire) || !g_odstEngineTlsIndex)
        {
            lastLogMs = 0;
            seen = 0;
            return;
        }
        uint64_t game = 0;
        bool read = false;
        __try
        {
            auto** slots = reinterpret_cast<void**>(__readgsqword(0x58));
            auto* tls = slots ? reinterpret_cast<unsigned char*>(slots[*g_odstEngineTlsIndex]) : nullptr;
            auto* users = tls ? *reinterpret_cast<unsigned char**>(tls + kOdstActionTestTlsMember) : nullptr;
            if (users)
            {
                game = *reinterpret_cast<volatile uint64_t*>(users); // user 0
                read = true;
                (void)kOdstActionTestUserStride;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("the ODST pod look tick faulted");
            g_odstLookTestProven.store(false, std::memory_order_release);
            LOG("ODST pod look-around: FAULTED reading the action-test "
                "accumulator; diagnostic disarmed");
            return;
        }
        seen |= game;
        if (!lastLogMs)
            lastLogMs = nowMs;
        if (nowMs - lastLogMs >= 2000)
        {
            const head_look_test::PollSnapshot polls = head_look_test::Snapshot(g_odstLookPolls, nowMs);
            uint64_t polled = polls.polled[head_look_test::PollAll] ? head_look_test::kAllBits : 0;
            for (int i = 0; i < 4; ++i)
                if (polls.polled[i])
                    polled |= head_look_test::kUpBit << i;
            char f[5], s[5], p[5];
            head_look_test::Letters(g_odstPodLookFedBits.exchange(0, std::memory_order_relaxed), f);
            head_look_test::Letters(seen, s);
            head_look_test::Letters(polled, p);
            LOG("ODST pod look-around: script polls %s%s, head look fed %s on "
                "%llu tick(s) (feed %s), game look tests seen %s (read=%d); "
                "servo stick frames U/D/L/R %u/%u/%u/%u",
                p, polls.polled[head_look_test::PollAll] ? " (all directions)" : "", f,
                static_cast<unsigned long long>(g_odstPodLookFedTicks.exchange(0, std::memory_order_relaxed)),
                g_odstPodLookFeedReady.load(std::memory_order_acquire) ? (g_config.odst_pod_head_look ? "on" : "off")
                                                                       : "unavailable",
                s, read ? 1 : 0, g_odstPodServoFrames[0].exchange(0, std::memory_order_relaxed),
                g_odstPodServoFrames[1].exchange(0, std::memory_order_relaxed),
                g_odstPodServoFrames[2].exchange(0, std::memory_order_relaxed),
                g_odstPodServoFrames[3].exchange(0, std::memory_order_relaxed));
            lastLogMs = nowMs;
            seen = 0;
        }
    }

    // Bind ODST's own vehicle-detection identities. Every address is
    // resolved by a signature unique to halo3odst.dll (and absent from
    // halo3.dll), and the engine TLS index plus both tag globals are decoded
    // rip-relatively from the matched bodies, so no absolute address ships.
    // This binding is READ-ONLY evidence: nothing it resolves changes a camera,
    // and failure leaves ODST exactly as it behaves today.
    void ResolveOdstVehicleBinding(uintptr_t base, size_t size)
    {
        g_odstVehicleBinding.store(
            static_cast<uint8_t>(OdstVehicleBindingState::NotInstalled), std::memory_order_release);
        g_odstNodeBinding.store(static_cast<uint8_t>(OdstVehicleBindingState::NotInstalled), std::memory_order_release);
        g_odstEngineTlsIndex = nullptr;
        g_odstPlayerUnitGetter = nullptr;
        g_odstUnitInVehicle = nullptr;
        g_odstVehicleTypeAccessor = nullptr;
        g_odstInterpolatedNodes = nullptr;
        g_odstMarkersInternal = nullptr;
        g_odstTagInstanceTable = nullptr;
        g_odstTagDataBase = nullptr;

        const uintptr_t nativeHit = sig::Find(base, size, kOdstUnitInVehicleSig);
        const uintptr_t getterHit = sig::Find(base, size, kOdstPlayerUnitGetterSig);
        const uintptr_t typeHit = sig::Find(base, size, kOdstVehicleTypeAccessorSig);
        const bool nativeUnique =
            nativeHit && !sig::Find(nativeHit + 1, base + size - nativeHit - 1, kOdstUnitInVehicleSig);
        const bool getterUnique =
            getterHit && !sig::Find(getterHit + 1, base + size - getterHit - 1, kOdstPlayerUnitGetterSig);
        const bool typeUnique =
            typeHit && !sig::Find(typeHit + 1, base + size - typeHit - 1, kOdstVehicleTypeAccessorSig);
        if (!nativeUnique || !getterUnique || !typeUnique)
        {
            g_odstVehicleBinding.store(
                static_cast<uint8_t>(OdstVehicleBindingState::StockFallback), std::memory_order_release);
            LOG("ODST vehicle evidence: signature resolve failed "
                "(native=%d/%d getter=%d/%d type=%d/%d found/unique); no "
                "vehicle evidence is collected and every ODST behavior is "
                "unchanged",
                nativeHit ? 1 : 0, nativeUnique ? 1 : 0, getterHit ? 1 : 0, getterUnique ? 1 : 0, typeHit ? 1 : 0,
                typeUnique ? 1 : 0);
            return;
        }

        // The TLS index lives in the matched native body: the `mov edx,[rip+d]`
        // at +0x1A ends at +0x20. Decode
        // it here rather than shipping +0xA8FB1C.
        const uintptr_t tlsInstruction = nativeHit + 0x1A;
        uint32_t* tlsIndex =
            reinterpret_cast<uint32_t*>(tlsInstruction + 6 + *reinterpret_cast<const int32_t*>(tlsInstruction + 2));
        const uintptr_t tlsAddress = reinterpret_cast<uintptr_t>(tlsIndex);
        // Same self-anchoring the Halo 3 accessor uses: instance table from the
        // disp32 at body +3, tag data base from the one at +25.
        void** instanceTable = reinterpret_cast<void**>(typeHit + 7 + *reinterpret_cast<const int32_t*>(typeHit + 3));
        void** tagBase = reinterpret_cast<void**>(typeHit + 29 + *reinterpret_cast<const int32_t*>(typeHit + 25));
        const uintptr_t instanceAddress = reinterpret_cast<uintptr_t>(instanceTable);
        const uintptr_t tagBaseAddress = reinterpret_cast<uintptr_t>(tagBase);
        if (tlsAddress < base || tlsAddress + sizeof(uint32_t) > base + size || *tlsIndex >= 256 ||
            instanceAddress < base || instanceAddress + sizeof(void*) > base + size || tagBaseAddress < base ||
            tagBaseAddress + sizeof(void*) > base + size)
        {
            g_odstVehicleBinding.store(
                static_cast<uint8_t>(OdstVehicleBindingState::StockFallback), std::memory_order_release);
            LOG("ODST vehicle evidence: decoded globals failed their in-module "
                "bounds proof (tls=+0x%llX instance=+0x%llX tagBase=+0x%llX); "
                "no vehicle evidence is collected",
                (unsigned long long)(tlsAddress - base), (unsigned long long)(instanceAddress - base),
                (unsigned long long)(tagBaseAddress - base));
            return;
        }

        g_odstEngineTlsIndex = tlsIndex;
        g_odstUnitInVehicle = reinterpret_cast<Halo3UnitInVehicleFn>(nativeHit);
        g_odstPlayerUnitGetter = reinterpret_cast<Halo3PlayerUnitGetterFn>(getterHit);
        g_odstVehicleTypeAccessor = reinterpret_cast<Halo3VehicleTypeFn>(typeHit);
        g_odstTagInstanceTable = instanceTable;
        g_odstTagDataBase = tagBase;
        g_odstVehicleBinding.store(static_cast<uint8_t>(OdstVehicleBindingState::Installed), std::memory_order_release);
        ResolveOdstLookTestProof(base, size, tlsAddress);
        LOG("ODST vehicle evidence: resolved unit_in_vehicle=+0x%llX "
            "player_unit=+0x%llX type_accessor=+0x%llX "
            "[unique; expected +0x%llX/+0x%llX/+0x%llX]; decoded tls=+0x%llX "
            "instanceTable=+0x%llX tagBase=+0x%llX [expected tls +0x%llX]",
            (unsigned long long)(nativeHit - base), (unsigned long long)(getterHit - base),
            (unsigned long long)(typeHit - base), (unsigned long long)kOdstUnitInVehicleNativeRva,
            (unsigned long long)kOdstPlayerUnitGetterRva, (unsigned long long)kOdstVehicleTypeAccessorRva,
            (unsigned long long)(tlsAddress - base), (unsigned long long)(instanceAddress - base),
            (unsigned long long)(tagBaseAddress - base), (unsigned long long)kOdstEngineTlsIndexRva);

        // Exposure lock: a third independent transaction. Losing it costs only
        // the steady exposure in a seat.
        g_odstExposureLock = nullptr;
        const uintptr_t exposureHit = sig::Find(base, size, kOdstExposureLockSig);
        const bool exposureUnique =
            exposureHit && !sig::Find(exposureHit + 1, base + size - exposureHit - 1, kOdstExposureLockSig);
        if (exposureUnique)
        {
            const uintptr_t lockAddress = exposureHit + 7 + *reinterpret_cast<const int32_t*>(exposureHit + 3);
            if (lockAddress >= base && lockAddress + 1 <= base + size)
            {
                g_odstExposureLock = reinterpret_cast<uint8_t*>(lockAddress);
                LOG("ODST vehicle exposure: render_exposure_lock resolved at "
                    "+0x%llX (from the adaptation test at +0x%llX); seat views "
                    "will hold a steady exposure",
                    (unsigned long long)(lockAddress - base), (unsigned long long)(exposureHit - base));
            }
        }
        if (!g_odstExposureLock)
        {
            LOG("ODST vehicle exposure: adaptation-lock signature "
                "missing/ambiguous (found=%d unique=%d); the dash will keep "
                "driving auto-exposure and everything else is unaffected",
                exposureHit ? 1 : 0, exposureUnique ? 1 : 0);
        }

        // The editable copy is what makes the lock stick. The engine rebuilds
        // the internal copy from it once per rendered frame, between our write
        // and the integrator's only read, so the internal write alone is
        // erased before it is ever consulted. Accept this only if the copy's
        // own decode of the internal block lands on the exact address the
        // independent adaptation-test signature above produced.
        g_odstExposureEditableLock = nullptr;
        const uintptr_t acceptHit = sig::Find(base, size, kOdstPostprocessAcceptSig);
        const bool acceptUnique =
            acceptHit && !sig::Find(acceptHit + 1, base + size - acceptHit - 1, kOdstPostprocessAcceptSig);
        if (acceptUnique && g_odstExposureLock)
        {
            const uintptr_t editableBase = acceptHit + 7 + *reinterpret_cast<const int32_t*>(acceptHit + 3);
            const uintptr_t internalBase = acceptHit + 14 + *reinterpret_cast<const int32_t*>(acceptHit + 10);
            const uintptr_t editableLock = acceptHit + 50 + *reinterpret_cast<const int32_t*>(acceptHit + 46);
            const uintptr_t internalLock = acceptHit + 58 + *reinterpret_cast<const int32_t*>(acceptHit + 54);
            const bool inModule = editableBase >= base && editableLock + 1 <= base + size && internalBase >= base &&
                                  internalLock + 1 <= base + size;
            // m_auto_exposure_lock is the last field of the 0x38-byte block.
            const bool shaped = editableLock == editableBase + 0x30 && internalLock == internalBase + 0x30;
            // The cross-check that replaces a hardcoded address: two unrelated
            // signatures must name the same byte.
            const bool agrees = internalLock == reinterpret_cast<uintptr_t>(g_odstExposureLock);
            if (inModule && shaped && agrees)
            {
                g_odstExposureEditableLock = reinterpret_cast<uint8_t*>(editableLock);
                LOG("ODST vehicle exposure: settings-block copy at +0x%llX "
                    "resolves editable=+0x%llX internal=+0x%llX; the internal "
                    "address agrees with the adaptation test, so the seat hold "
                    "writes the copy the engine propagates from",
                    (unsigned long long)(acceptHit - base), (unsigned long long)(editableLock - base),
                    (unsigned long long)(internalLock - base));
            }
            else
            {
                LOG("ODST vehicle exposure: settings-block copy failed its "
                    "cross-check (inModule=%d shaped=%d agrees=%d; "
                    "editable=+0x%llX internal=+0x%llX vs test +0x%llX); the "
                    "seat hold is left OFF rather than writing a byte we "
                    "cannot vouch for",
                    inModule ? 1 : 0, shaped ? 1 : 0, agrees ? 1 : 0, (unsigned long long)(editableLock - base),
                    (unsigned long long)(internalLock - base),
                    (unsigned long long)(reinterpret_cast<uintptr_t>(g_odstExposureLock) - base));
                // Without the editable copy the internal write is provably
                // inert, so claiming a steady exposure would be a silent
                // degrade. Refuse the whole feature instead.
                g_odstExposureLock = nullptr;
            }
        }
        else if (!g_odstExposureEditableLock)
        {
            LOG("ODST vehicle exposure: settings-block copy signature "
                "missing/ambiguous (found=%d unique=%d lock=%d); the seat hold "
                "is left OFF because writing only the internal copy is erased "
                "every frame before the engine reads it",
                acceptHit ? 1 : 0, acceptUnique ? 1 : 0, g_odstExposureLock ? 1 : 0);
            g_odstExposureLock = nullptr;
        }

        // The shot line. Resolve the camera-position evaluator and the weapon
        // barrel function that calls it, then find the ONE call inside that
        // barrel function which targets the evaluator. Its return address is
        // what tells the hook it is on the firing path rather than on one of
        // the other consumers -- including the engine's own first-person
        // camera placement, which must keep the stock value.
        if (g_origOdstUnitCameraPosition)
        {
            MH_DisableHook(g_odstUnitCameraPositionTarget);
            MCCVR_RemoveHookChecked(g_odstUnitCameraPositionTarget);
            g_origOdstUnitCameraPosition = nullptr;
            g_odstUnitCameraPositionTarget = nullptr;
        }
        g_odstFiringOriginReturn = 0;
        const uintptr_t camPosHit = sig::Find(base, size, kOdstUnitCameraPositionSig);
        const bool camPosUnique =
            camPosHit && !sig::Find(camPosHit + 1, base + size - camPosHit - 1, kOdstUnitCameraPositionSig);
        const uintptr_t barrelHit = sig::Find(base, size, kOdstWeaponBarrelFiringSig);
        const bool barrelUnique =
            barrelHit && !sig::Find(barrelHit + 1, base + size - barrelHit - 1, kOdstWeaponBarrelFiringSig);
        uintptr_t firingCallSite = 0;
        uint32_t firingCallCount = 0;
        if (camPosUnique && barrelUnique)
        {
            // Bounded walk of the barrel function's body looking for
            // `E8 rel32` whose target is the evaluator. Requiring exactly one
            // is what makes the return address unambiguous.
            const uintptr_t scanEnd = (barrelHit + 0x400 < base + size) ? barrelHit + 0x400 : base + size - 5;
            for (uintptr_t p = barrelHit; p < scanEnd; ++p)
            {
                if (*reinterpret_cast<const uint8_t*>(p) != 0xE8)
                    continue;
                const int32_t rel = *reinterpret_cast<const int32_t*>(p + 1);
                if (p + 5 + rel == camPosHit)
                {
                    firingCallSite = p;
                    ++firingCallCount;
                }
            }
        }
        if (firingCallCount == 1)
        {
            g_odstUnitCameraPositionTarget = reinterpret_cast<void*>(camPosHit);
            const MH_STATUS created = MCCVR_CreateHookChecked(g_odstUnitCameraPositionTarget,
                reinterpret_cast<void*>(&OdstUnitCameraPositionHook),
                reinterpret_cast<void**>(&g_origOdstUnitCameraPosition));
            const bool enabled = created == MH_OK && MH_EnableHook(g_odstUnitCameraPositionTarget) == MH_OK;
            if (enabled)
            {
                g_odstFiringOriginReturn = firingCallSite + 5;
                LOG("ODST seat aim: shot origin follows the rendered eye "
                    "(evaluator +0x%llX, barrel +0x%llX, firing call +0x%llX); "
                    "only that one call site moves, so the engine camera, the "
                    "hands and cutscenes keep the stock position",
                    (unsigned long long)(camPosHit - base), (unsigned long long)(barrelHit - base),
                    (unsigned long long)(firingCallSite - base));
            }
            else
            {
                MCCVR_RemoveHookChecked(g_odstUnitCameraPositionTarget);
                g_origOdstUnitCameraPosition = nullptr;
                g_odstUnitCameraPositionTarget = nullptr;
                LOG("ODST seat aim: FAILED to install the shot-origin hook "
                    "(create=%d); seated shots keep firing from the engine's "
                    "own eye point and will not follow the crosshair exactly",
                    (int)created);
            }
        }
        else
        {
            LOG("ODST seat aim: shot-origin signatures missing/ambiguous "
                "(evaluator found=%d unique=%d, barrel found=%d unique=%d, "
                "firing calls=%u); seated shots keep firing from the engine's "
                "own eye point and will not follow the crosshair exactly",
                camPosHit ? 1 : 0, camPosUnique ? 1 : 0, barrelHit ? 1 : 0, barrelUnique ? 1 : 0, firingCallCount);
        }

        // The render-node pair is a second, independent transaction: both
        // patterns also match once in halo3.dll, so they are only meaningful
        // because the camera core already pinned this module's identity.
        // Losing them costs the node/marker evidence, not the binding above.
        const uintptr_t nodeHit = sig::Find(base, size, kOdstInterpolatedNodesSig);
        const uintptr_t markerHit = sig::Find(base, size, kOdstMarkersInternalSig);
        const bool nodeUnique =
            nodeHit && !sig::Find(nodeHit + 1, base + size - nodeHit - 1, kOdstInterpolatedNodesSig);
        const bool markerUnique =
            markerHit && !sig::Find(markerHit + 1, base + size - markerHit - 1, kOdstMarkersInternalSig);
        if (nodeUnique && markerUnique)
        {
            g_odstInterpolatedNodes = reinterpret_cast<Halo3InterpolatedNodesFn>(nodeHit);
            g_odstMarkersInternal = reinterpret_cast<Halo3MarkersInternalFn>(markerHit);
            g_odstNodeBinding.store(
                static_cast<uint8_t>(OdstVehicleBindingState::Installed), std::memory_order_release);
            LOG("ODST vehicle render nodes: resolved provider=+0x%llX "
                "markers=+0x%llX [unique; expected +0x%llX/+0x%llX]",
                (unsigned long long)(nodeHit - base), (unsigned long long)(markerHit - base),
                (unsigned long long)kOdstInterpolatedNodesRva, (unsigned long long)kOdstMarkersInternalRva);
        }
        else
        {
            g_odstNodeBinding.store(
                static_cast<uint8_t>(OdstVehicleBindingState::StockFallback), std::memory_order_release);
            LOG("ODST vehicle render nodes: signature resolve failed "
                "(provider=%d/%d markers=%d/%d found/unique); node evidence "
                "absent, base vehicle evidence unaffected",
                nodeHit ? 1 : 0, nodeUnique ? 1 : 0, markerHit ? 1 : 0, markerUnique ? 1 : 0);
        }
    }

    bool ValidateOdstCameraLayout()
    {
        const auto& layout = kOdstCameraProfile.layout;
        const bool frontTiled = layout.rootCurrentCompact + layout.compactSize == layout.rootCurrentDerived &&
                                layout.rootCurrentDerived + layout.derivedSize == layout.rootSecondaryCompact &&
                                layout.rootSecondaryCompact + layout.compactSize == layout.rootSecondaryDerived &&
                                layout.rootSecondaryDerived + layout.derivedSize == 0x2A8;
        const bool nestedMatches = layout.nestedFpBase + layout.rootCurrentCompact == layout.nestedCurrentCompact &&
                                   layout.nestedFpBase + layout.rootCurrentDerived == layout.nestedCurrentDerived &&
                                   layout.nestedFpBase + layout.rootSecondaryCompact == layout.nestedSecondaryCompact &&
                                   layout.nestedFpBase + layout.rootSecondaryDerived == layout.nestedSecondaryDerived &&
                                   layout.nestedFpBase + 0x2A8 == layout.nestedSourceCamera;
        const bool boundsFit = layout.sourceReferenceFov + sizeof(float) <= 0x90 &&
                               layout.compactUp + sizeof(float) * 3 <= layout.compactSize &&
                               layout.compactModeFlags + sizeof(uint32_t) <= layout.compactSize &&
                               layout.compactFpBlend + sizeof(float) <= layout.compactSize &&
                               layout.windowBounds + sizeof(int16_t) * 4 <= layout.compactSize &&
                               layout.renderBounds + sizeof(int16_t) * 4 <= layout.compactSize &&
                               layout.activeBounds + sizeof(int16_t) * 4 <= layout.compactSize &&
                               layout.verticalFov + sizeof(float) <= layout.compactSize &&
                               layout.referenceFov + sizeof(float) <= layout.compactSize &&
                               layout.verticalOffset + sizeof(float) <= layout.compactSize &&
                               layout.nearClip + sizeof(float) <= layout.compactSize &&
                               layout.farClip + sizeof(float) <= layout.compactSize &&
                               layout.obliquePlane + sizeof(float) * 4 <= layout.compactSize &&
                               layout.customProjection + sizeof(uint32_t) <= layout.compactSize &&
                               layout.customProjectionData + sizeof(float) * 4 <= layout.compactSize &&
                               layout.projectionMatrix + sizeof(float) * 16 <= layout.derivedSize &&
                               layout.normalizedViewport + sizeof(float) * 4 <= layout.viewStride &&
                               layout.constructedViewSlot + sizeof(int32_t) <= layout.viewStride &&
                               layout.viewCount + sizeof(int32_t) <= layout.viewStride &&
                               layout.additionalContext + sizeof(int32_t) <= layout.viewStride &&
                               layout.renderUserIndex + sizeof(int32_t) <= layout.viewStride &&
                               layout.initializedZero + sizeof(int32_t) <= layout.viewStride &&
                               layout.finalTailBoolean + sizeof(uint8_t) <= layout.viewStride;
        if (layout.compactSize != sizeof(g_odstCamera.eyeCompactCamera) ||
            layout.derivedSize != sizeof(g_odstCamera.eyeDerivedBlock) || layout.viewStride != 0x2810 || !frontTiled ||
            !nestedMatches || !boundsFit)
        {
            LOG("ODST camera preflight: internal layout profile invariant failed");
            return false;
        }
        return true;
    }

    bool ResolveOdstTextRange(
        uintptr_t base, size_t size, uintptr_t& textBegin, uintptr_t& textEnd, uint32_t& timestamp, size_t& imageSize)
    {
        if (!base || size < sizeof(IMAGE_DOS_HEADER) || size > UINTPTR_MAX - base)
            return false;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
            static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > size)
            return false;
        const size_t ntOffset = static_cast<size_t>(dos->e_lfanew);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + ntOffset);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt->FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64))
            return false;
        timestamp = nt->FileHeader.TimeDateStamp;
        imageSize = nt->OptionalHeader.SizeOfImage;
        const size_t sectionTableOffset =
            ntOffset + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt->FileHeader.SizeOfOptionalHeader;
        if (sectionTableOffset > size ||
            nt->FileHeader.NumberOfSections > (size - sectionTableOffset) / sizeof(IMAGE_SECTION_HEADER))
            return false;
        const IMAGE_SECTION_HEADER* sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(base + sectionTableOffset);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        {
            const IMAGE_SECTION_HEADER& section = sections[i];
            if (memcmp(section.Name, ".text", 5) != 0 || !(section.Characteristics & IMAGE_SCN_MEM_EXECUTE))
                continue;
            const size_t sectionSize =
                section.Misc.VirtualSize > section.SizeOfRawData ? section.Misc.VirtualSize : section.SizeOfRawData;
            const size_t sectionOffset = section.VirtualAddress;
            if (!sectionSize || sectionOffset >= size || sectionSize > size - sectionOffset)
                return false;
            textBegin = base + sectionOffset;
            textEnd = textBegin + sectionSize;
            return true;
        }
        return false;
    }

    bool FindUniqueOdstRole(uintptr_t base, size_t size, uintptr_t textBegin, uintptr_t textEnd, const char* role,
        const char* pattern, uintptr_t& result)
    {
        result = sig::Find(base, size, pattern);
        const uintptr_t moduleEnd = base + size;
        if (!result || result < textBegin || result >= textEnd)
        {
            LOG("ODST camera preflight: %s signature missing or outside .text", role);
            result = 0;
            return false;
        }
        const uintptr_t next = result + 1;
        if (next < moduleEnd && sig::Find(next, moduleEnd - next, pattern))
        {
            LOG("ODST camera preflight: %s signature is ambiguous", role);
            result = 0;
            return false;
        }
        return true;
    }

    OdstInstallResult PreflightOdstCameraRuntime(uintptr_t base, size_t size, OdstResolvedCameraRuntime& resolved)
    {
        if (!ValidateOdstCameraLayout())
            return OdstInstallResult::Failed;

        uintptr_t textBegin = 0, textEnd = 0;
        uint32_t timestamp = 0;
        size_t imageSize = 0;
        if (!ResolveOdstTextRange(base, size, textBegin, textEnd, timestamp, imageSize))
        {
            LOG("ODST camera preflight: invalid loaded PE image");
            return OdstInstallResult::Failed;
        }
        if (timestamp != kOdstCameraProfile.expectedTimestamp || imageSize != kOdstCameraProfile.expectedImageSize ||
            imageSize != size)
        {
            LOG("ODST camera preflight: module identity mismatch "
                "(timestamp 0x%08X, image 0x%zX; expected 0x%08X/0x%zX)",
                timestamp, imageSize, kOdstCameraProfile.expectedTimestamp, kOdstCameraProfile.expectedImageSize);
            return OdstInstallResult::Failed;
        }

        const uintptr_t moduleEnd = base + size;
        const size_t arraySize = 4 * kOdstCameraProfile.layout.viewStride;
        const bool cachedStatic = g_odstPreflightCache.valid && g_odstPreflightCache.moduleBase == base &&
                                  g_odstPreflightCache.moduleSize == size;
        if (cachedStatic)
            resolved = g_odstPreflightCache.resolved;
        else
        {
            ClearOdstStaticPreflightCache();
            bool signaturesOk = true;
            signaturesOk &= FindUniqueOdstRole(
                base, size, textBegin, textEnd, "camera copy", kOdstCameraProfile.camCopyPattern, resolved.camCopy);
            signaturesOk &= FindUniqueOdstRole(base, size, textBegin, textEnd, "post-observer camera effect",
                kObserverCameraEffectSig, resolved.observerCameraEffect);
            signaturesOk &= FindUniqueOdstRole(base, size, textBegin, textEnd, "inner view renderer",
                kOdstCameraProfile.renderViewPattern, resolved.renderView);
            signaturesOk &= FindUniqueOdstRole(base, size, textBegin, textEnd, "prepare view",
                kOdstCameraProfile.prepareViewPattern, resolved.prepareView);
            signaturesOk &= FindUniqueOdstRole(base, size, textBegin, textEnd, "viewport builder",
                kOdstCameraProfile.buildViewportPattern, resolved.buildViewport);
            signaturesOk &= FindUniqueOdstRole(base, size, textBegin, textEnd, "matrix builder",
                kOdstCameraProfile.buildMatricesPattern, resolved.buildMatrices);
            signaturesOk &= FindUniqueOdstRole(base, size, textBegin, textEnd, "FP camera rebuild",
                kOdstCameraProfile.fpCameraRebuildPattern, resolved.fpCameraRebuild);
            signaturesOk &= FindUniqueOdstRole(base, size, textBegin, textEnd, "FP camera uploader",
                kOdstCameraProfile.fpCameraUploadPattern, resolved.fpCameraUpload);
            signaturesOk &= FindUniqueOdstRole(
                base, size, textBegin, textEnd, "FP driver", kOdstCameraProfile.fpDriverPattern, resolved.fpDriver);
            signaturesOk &= FindUniqueOdstRole(
                base, size, textBegin, textEnd, "FP interpolation", kOdstFpInterpolateSig, resolved.fpInterpolate);
            signaturesOk &= FindUniqueOdstRole(base, size, textBegin, textEnd, "FP visible palette",
                kOdstFpVisiblePaletteSig, resolved.fpVisiblePalette);
            signaturesOk &= FindUniqueOdstRole(base, size, textBegin, textEnd, "native FP weapon-IK decision",
                kOdstNativeWeaponIkDecisionSig, resolved.nativeWeaponIkDecision);
            signaturesOk &= FindUniqueOdstRole(base, size, textBegin, textEnd, "FP driver guard",
                kOdstCameraProfile.fpDriverGuardPattern, resolved.fpDriverGuard);
            signaturesOk &= FindUniqueOdstRole(base, size, textBegin, textEnd, "four-slot camera constructor",
                kOdstCameraProfile.gunCameraConstructorPattern, resolved.gunCameraConstructor);
            signaturesOk &= FindUniqueOdstRole(base, size, textBegin, textEnd, "native pause owner",
                kOdstCameraProfile.nativePauseOwnerPattern, resolved.nativePauseOwner);
            signaturesOk &= FindUniqueOdstRole(base, size, textBegin, textEnd, "native CHUD phase primary",
                kOdstHudPhasePrimarySig, resolved.hudPhasePrimary);
            signaturesOk &= FindUniqueOdstRole(base, size, textBegin, textEnd, "native CHUD phase secondary",
                kOdstHudPhaseSecondarySig, resolved.hudPhaseSecondary);
            signaturesOk &= FindUniqueOdstRole(base, size, textBegin, textEnd, "native CHUD target copy",
                kOdstHudTargetCopySig, resolved.hudTargetCopy);
            resolved.motionBlurScale = FindDebugVarFloat(base, size, "motion_blur_scale");
            resolved.motionBlurMax = FindDebugVarFloat(base, size, "motion_blur_max");
            const bool motionBlurOk =
                resolved.motionBlurScale && resolved.motionBlurMax &&
                resolved.motionBlurScale != resolved.motionBlurMax &&
                reinterpret_cast<uintptr_t>(resolved.motionBlurScale) >= base &&
                reinterpret_cast<uintptr_t>(resolved.motionBlurScale) + sizeof(float) <= moduleEnd &&
                reinterpret_cast<uintptr_t>(resolved.motionBlurMax) >= base &&
                reinterpret_cast<uintptr_t>(resolved.motionBlurMax) + sizeof(float) <= moduleEnd;
            if (!motionBlurOk)
                LOG("ODST camera preflight: title-native motion-blur scale/max vars unavailable");

            bool nativePauseOk = false;
            if (resolved.nativePauseOwner)
            {
                // The unique ODST owner signature starts at +0xBCA5. Its final
                // instruction is C6 05 disp32 01 at +0x20 and writes the native
                // pause byte used throughout halo3odst.dll.
                const uintptr_t write = resolved.nativePauseOwner + 0x20;
                if (write + 7 <= moduleEnd && *reinterpret_cast<const uint8_t*>(write) == 0xC6 &&
                    *reinterpret_cast<const uint8_t*>(write + 1) == 0x05 &&
                    *reinterpret_cast<const uint8_t*>(write + 6) == 0x01)
                {
                    const int32_t displacement = *reinterpret_cast<const int32_t*>(write + 2);
                    const uintptr_t flag = write + 7 + displacement;
                    if (flag >= base && flag < moduleEnd)
                    {
                        const uint8_t initial = *reinterpret_cast<const uint8_t*>(flag);
                        if (initial <= 1)
                        {
                            resolved.nativePauseFlag = reinterpret_cast<uint8_t*>(flag);
                            nativePauseOk = true;
                        }
                    }
                }
            }
            if (!nativePauseOk)
                LOG("ODST camera preflight: native pause owner/flag proof failed");
            if (!signaturesOk || !motionBlurOk || !nativePauseOk)
                return OdstInstallResult::Failed;

            const uintptr_t guardCall = resolved.fpDriverGuard + 13;
            if (*reinterpret_cast<const uint8_t*>(guardCall) != 0xE8)
            {
                LOG("ODST camera preflight: FP guard call opcode changed");
                return OdstInstallResult::Failed;
            }
            const int32_t guardDisplacement = *reinterpret_cast<const int32_t*>(guardCall + 1);
            const uintptr_t guardTarget = guardCall + 5 + guardDisplacement;
            if (guardTarget != resolved.fpDriver)
            {
                LOG("ODST camera preflight: FP guard does not call the resolved driver");
                return OdstInstallResult::Failed;
            }

            const uintptr_t constructor = resolved.gunCameraConstructor;
            const int32_t arrayDisplacement = *reinterpret_cast<const int32_t*>(constructor + 13);
            resolved.gunCameraArray = constructor + 17 + arrayDisplacement;
            if (resolved.gunCameraArray < base || resolved.gunCameraArray > moduleEnd ||
                arraySize > moduleEnd - resolved.gunCameraArray)
            {
                LOG("ODST camera preflight: four-slot camera array is outside "
                    "halo3odst.dll");
                return OdstInstallResult::Failed;
            }
            g_odstPreflightCache.valid = true;
            g_odstPreflightCache.moduleBase = base;
            g_odstPreflightCache.moduleSize = size;
            g_odstPreflightCache.resolved = resolved;
        }

        bool allConstructed = true;
        for (size_t slot = 0; slot < 4; ++slot)
        {
            const uintptr_t slotAddress = resolved.gunCameraArray + slot * kOdstCameraProfile.layout.viewStride;
            const uintptr_t vtable = *reinterpret_cast<const uintptr_t*>(slotAddress);
            if (!vtable)
                allConstructed = false;
            else if (vtable < base || vtable >= moduleEnd)
            {
                LOG("ODST camera preflight: camera slot %zu vtable is outside module", slot);
                return OdstInstallResult::Failed;
            }
        }
        if (!allConstructed)
        {
            if (!g_odstCamera.waitingLogged.exchange(true))
                LOG("ODST camera preflight passed; waiting for the four-slot camera array to construct");
            return OdstInstallResult::NotReady;
        }

        const auto& layout = kOdstCameraProfile.layout;
        if (!OdstCameraArraySupportsBringup(resolved.gunCameraArray))
        {
            if (!g_odstCamera.waitingLogged.exchange(true))
            {
                LOG("ODST camera preflight passed; waiting for the proven "
                    "slot-0/user-0 ordinary camera mode");
                LogOdstCameraReadiness(resolved.gunCameraArray);
            }
            return OdstInstallResult::NotReady;
        }

        g_odstCamera.waitingLogged.store(false, std::memory_order_release);
        LOG("ODST camera preflight: profile '%s', PE timestamp 0x%08X, "
            "image 0x%zX, compact 0x%zX, derived 0x%zX, stride 0x%zX",
            kOdstCameraProfile.displayName, timestamp, imageSize, layout.compactSize, layout.derivedSize,
            layout.viewStride);
        LOG("ODST camera RVAs: copy=%llX render=%llX prepare=%llX "
            "viewport=%llX matrices=%llX",
            static_cast<unsigned long long>(resolved.camCopy - base),
            static_cast<unsigned long long>(resolved.renderView - base),
            static_cast<unsigned long long>(resolved.prepareView - base),
            static_cast<unsigned long long>(resolved.buildViewport - base),
            static_cast<unsigned long long>(resolved.buildMatrices - base));
        LOG("ODST camera RVAs: fpRebuild=%llX fpUpload=%llX fpDriver=%llX "
            "fpGuard=%llX constructor=%llX array=%llX",
            static_cast<unsigned long long>(resolved.fpCameraRebuild - base),
            static_cast<unsigned long long>(resolved.fpCameraUpload - base),
            static_cast<unsigned long long>(resolved.fpDriver - base),
            static_cast<unsigned long long>(resolved.fpDriverGuard - base),
            static_cast<unsigned long long>(resolved.gunCameraConstructor - base),
            static_cast<unsigned long long>(resolved.gunCameraArray - base));
        LOG("ODST pause evidence: owner=%llX flag=%llX (initial=%u)",
            static_cast<unsigned long long>(resolved.nativePauseOwner - base),
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(resolved.nativePauseFlag) - base),
            static_cast<unsigned>(*resolved.nativePauseFlag));
        LOG("ODST comfort evidence: observerEffect=%llX blurScale=%llX "
            "blurMax=%llX (stock %.4f/%.4f)",
            static_cast<unsigned long long>(resolved.observerCameraEffect - base),
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(resolved.motionBlurScale) - base),
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(resolved.motionBlurMax) - base),
            *resolved.motionBlurScale, *resolved.motionBlurMax);
        return OdstInstallResult::Installed;
    }

    bool OdstDisableStatusIsSafe(MH_STATUS status)
    {
        return status == MH_OK || status == MH_ERROR_DISABLED || status == MH_ERROR_NOT_CREATED;
    }

    struct OdstCodeRange
    {
        DWORD64 begin = 0;
        DWORD64 end = 0;
    };

    struct OdstFrozenThread
    {
        HANDLE handle = nullptr;
        DWORD threadId = 0;
        bool suspended = false;
    };

    class OdstThreadFreeze
    {
    public:
        ~OdstThreadFreeze() { Release(); }

        bool Capture()
        {
            if (!m_threads.empty())
                return false;

            const DWORD processId = GetCurrentProcessId();
            const DWORD currentThreadId = GetCurrentThreadId();
            HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snapshot == INVALID_HANDLE_VALUE)
                return false;

            bool enumerateOk = true;
            THREADENTRY32 entry{};
            entry.dwSize = sizeof(entry);
            if (!Thread32First(snapshot, &entry))
                enumerateOk = false;
            while (enumerateOk)
            {
                if (entry.th32OwnerProcessID == processId && entry.th32ThreadID != currentThreadId)
                {
                    HANDLE thread =
                        OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION | SYNCHRONIZE,
                            FALSE, entry.th32ThreadID);
                    if (!thread)
                    {
                        if (GetLastError() != ERROR_INVALID_PARAMETER)
                            enumerateOk = false;
                    }
                    else
                    {
                        m_threads.push_back({thread, entry.th32ThreadID, false});
                    }
                }
                if (!Thread32Next(snapshot, &entry))
                {
                    if (GetLastError() != ERROR_NO_MORE_FILES)
                        enumerateOk = false;
                    break;
                }
            }
            CloseHandle(snapshot);
            if (!enumerateOk)
            {
                Release();
                return false;
            }

            // No heap work occurs after the first suspension. Holding every
            // captured thread at once makes the subsequent RIP/counter check a
            // single process-wide quiescence snapshot instead of a sequence of
            // observations that can race one another.
            for (OdstFrozenThread& thread : m_threads)
            {
                const DWORD previousCount = SuspendThread(thread.handle);
                if (previousCount == static_cast<DWORD>(-1))
                {
                    if (WaitForSingleObject(thread.handle, 0) != WAIT_OBJECT_0)
                    {
                        Release();
                        return false;
                    }
                    continue;
                }
                thread.suspended = true;
            }

            // A captured thread can create another thread before it is
            // suspended. Re-enumerate only after the captured set is frozen;
            // an unseen live thread aborts this attempt and is captured on the
            // next retry. Thread IDs cannot be reused while these handles live.
            if (!FrozenSnapshotIsComplete(processId, currentThreadId))
            {
                Release();
                return false;
            }
            return true;
        }

        bool Release()
        {
            bool ok = true;
            for (size_t i = m_threads.size(); i > 0; --i)
            {
                OdstFrozenThread& thread = m_threads[i - 1];
                if (thread.suspended && ResumeThread(thread.handle) == static_cast<DWORD>(-1) &&
                    WaitForSingleObject(thread.handle, 0) != WAIT_OBJECT_0)
                {
                    ok = false;
                }
                CloseHandle(thread.handle);
            }
            m_threads.clear();
            return ok;
        }

        const std::vector<OdstFrozenThread>& Threads() const { return m_threads; }

    private:
        bool FrozenSnapshotIsComplete(DWORD processId, DWORD currentThreadId) const
        {
            HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
            if (snapshot == INVALID_HANDLE_VALUE)
                return false;

            bool complete = true;
            THREADENTRY32 entry{};
            entry.dwSize = sizeof(entry);
            if (!Thread32First(snapshot, &entry))
                complete = false;
            while (complete)
            {
                if (entry.th32OwnerProcessID == processId && entry.th32ThreadID != currentThreadId)
                {
                    bool foundFrozen = false;
                    for (const OdstFrozenThread& thread : m_threads)
                    {
                        if (thread.threadId == entry.th32ThreadID)
                        {
                            foundFrozen = thread.suspended || WaitForSingleObject(thread.handle, 0) == WAIT_OBJECT_0;
                            break;
                        }
                    }
                    if (!foundFrozen)
                        complete = false;
                }
                if (!complete || !Thread32Next(snapshot, &entry))
                {
                    if (complete && GetLastError() != ERROR_NO_MORE_FILES)
                        complete = false;
                    break;
                }
            }
            CloseHandle(snapshot);
            return complete;
        }

        std::vector<OdstFrozenThread> m_threads;
    };

    bool ResolveOdstCodeRange(const void* function, OdstCodeRange& range)
    {
        DWORD64 imageBase = 0;
        const PRUNTIME_FUNCTION runtimeFunction =
            RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(function), &imageBase, nullptr);
        if (!runtimeFunction || runtimeFunction->EndAddress <= runtimeFunction->BeginAddress)
            return false;
        range.begin = imageBase + runtimeFunction->BeginAddress;
        range.end = imageBase + runtimeFunction->EndAddress;
        return true;
    }

    void* OdstTrampolineForHook(size_t index)
    {
        return const_cast<void*>(g_odstCamera.hooks.TrampolineAt(index));
    }

    bool ScanForOdstDetourIngress(std::atomic<int>& counter, bool renderOnly, bool& busy)
    {
        static bool rangesResolved = false;
        static OdstCodeRange ranges[15]{};
        if (!rangesResolved)
        {
            const void* functions[] = {
                reinterpret_cast<const void*>(&OdstCamCopyHook),
                reinterpret_cast<const void*>(&OdstRenderViewHook),
                reinterpret_cast<const void*>(&OdstFpCameraRebuildHook),
                reinterpret_cast<const void*>(&OdstFpDriverHook),
                reinterpret_cast<const void*>(&OdstObserverCameraEffectHook),
                reinterpret_cast<const void*>(&OdstFpInterpolateWeaponHook),
                reinterpret_cast<const void*>(&OdstFpVisiblePaletteWeaponHook),
                reinterpret_cast<const void*>(&OdstHudPhasePrimaryHook),
                reinterpret_cast<const void*>(&OdstHudPhaseSecondaryHook),
                reinterpret_cast<const void*>(&OdstHudTargetCopyHook),
                reinterpret_cast<const void*>(&OdstHudAnchorBasisHook),
                reinterpret_cast<const void*>(&OdstHudCrosshairVisibleHook),
                reinterpret_cast<const void*>(&OdstHudDrawWidgetHook),
                reinterpret_cast<const void*>(&OdstHudXformHook),
                reinterpret_cast<const void*>(&OdstCollisionResolveDetour),
            };
            static_assert(_countof(functions) == _countof(ranges));
            bool resolved = true;
            for (size_t i = 0; i < _countof(functions); ++i)
                resolved &= ResolveOdstCodeRange(functions[i], ranges[i]);
            if (!resolved)
            {
                LOG("ODST camera cleanup: could not resolve the complete ODST detour unwind ranges");
                return false;
            }
            rangesResolved = true;
        }

        OdstThreadFreeze frozenThreads;
        if (!frozenThreads.Capture())
            return false;

        busy = false;
        bool scanOk = true;
        for (const OdstFrozenThread& thread : frozenThreads.Threads())
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
            const size_t first = renderOnly ? 1 : 0;
            const size_t last = renderOnly ? 2 : _countof(ranges);
            for (size_t i = first; i < last; ++i)
            {
                if (instruction >= ranges[i].begin && instruction < ranges[i].end)
                {
                    busy = true;
                    break;
                }
            }

            // MinHook v1.3.4 x64 stores a hook's trampoline and relay in the
            // same 64-byte memory slot, whose base is the ppOriginal value.
            // A thread can be preempted in that relay before it reaches the
            // detour's counter, so the complete slot is part of ingress.
            for (size_t i = first; !busy && i < last && i < g_odstCamera.hooks.Count(); ++i)
            {
                if (!g_odstCamera.hooks.At(i))
                    continue;
                const DWORD64 trampoline = reinterpret_cast<DWORD64>(OdstTrampolineForHook(i));
                if (!trampoline)
                {
                    scanOk = false;
                    continue;
                }
                if (instruction >= trampoline && instruction < trampoline + 64)
                    busy = true;
            }
        }
        if (counter.load(std::memory_order_acquire) != 0)
            busy = true;
        if (!renderOnly && g_odstWorldCollision.callbacks.load(std::memory_order_acquire) != 0)
            busy = true;

        // All observed threads remain suspended through the RIP and counter
        // checks. Once hooks are disabled, an all-clear snapshot is stable:
        // after resume there is no patched target that can enter a relay.
        const bool resumed = frozenThreads.Release();
        return scanOk && resumed;
    }

    bool WaitForOdstCallbacks(std::atomic<int>& counter, const char* role, bool renderOnly)
    {
        for (unsigned waited = 0; waited < 2000; ++waited)
        {
            if (counter.load(std::memory_order_acquire) == 0 &&
                (renderOnly || g_odstWorldCollision.callbacks.load(std::memory_order_acquire) == 0))
            {
                bool ingressBusy = false;
                if (!ScanForOdstDetourIngress(counter, renderOnly, ingressBusy))
                {
                    LOG("ODST camera cleanup: %s ingress scan failed", role);
                    return false;
                }
                if (!ingressBusy && counter.load(std::memory_order_acquire) == 0 &&
                    (renderOnly || g_odstWorldCollision.callbacks.load(std::memory_order_acquire) == 0))
                    return true;
            }
            Sleep(1);
        }
        LOG("ODST camera cleanup: %s callbacks did not reach verified quiescence", role);
        return false;
    }

    bool DisableAndRemoveOdstHooks()
    {
        if (!RemoveOdstWeaponHaptics())
            return false;
        if (!RemoveOdstMuzzle())
            return false;
        if (!DisableAndRemoveOdstContactMelee())
            return false;
        // Stop new outer stereo transactions first. Existing ones retain all FP
        // dependencies until their complete two-eye callback has returned.
        if (g_odstCamera.renderHookTarget)
        {
            const MH_STATUS status = MH_DisableHook(g_odstCamera.renderHookTarget);
            if (!OdstDisableStatusIsSafe(status))
            {
                LOG("ODST camera cleanup: render disable failed for %p (%d)", g_odstCamera.renderHookTarget,
                    static_cast<int>(status));
                return false;
            }
        }
        if (!WaitForOdstCallbacks(g_odstCamera.activeRenderCallbacks, "outer-render", true))
            return false;

        g_odstCamera.armed.store(false, std::memory_order_release);
        g_odstWorldCollision.runtimeBoundsProven.store(false, std::memory_order_release);
        g_odstWorldCollision.installed.store(false, std::memory_order_release);
        PublishOdstLifecycle();
        g_odstCamera.eyeView.store(nullptr, std::memory_order_release);
        g_stereoEye.store(-1, std::memory_order_release);

        // The render hook was already disabled above, before its callbacks drained.
        const bool disabledAll = g_odstCamera.hooks.DisableAll([](void* target)
            { return target == g_odstCamera.renderHookTarget ? MH_OK : MH_DisableHook(target); },
            [](void* target, MH_STATUS status)
            { LOG("ODST camera cleanup: disable failed for %p (%d)", target, static_cast<int>(status)); });
        if (!disabledAll || !WaitForOdstCallbacks(g_odstCamera.activeCallbacks, "all-detour", false))
            return false;

        return g_odstCamera.hooks.RemoveAllNewestFirst([](void* target) { return MCCVR_RemoveHookChecked(target); },
            [](void* target, MH_STATUS status)
            { LOG("ODST camera cleanup: remove failed for %p (%d)", target, static_cast<int>(status)); });
    }

    void ReleaseOdstModuleReferenceAndClearPointers()
    {
        // ODST owns exactly one post-preflight loader reference. Keep it
        // until every hook is quiescent/removed and both direct native patches
        // have been restored; only then clear module-backed pointers and release
        // the pin exactly once. A title exit can no longer stale restoration
        // pointers while verified cleanup is still in progress.
        HMODULE retiredModuleReference = g_odstCamera.moduleReference;
        ClearOdstCameraPointers();
        if (retiredModuleReference)
            FreeLibrary(retiredModuleReference);
    }

    bool DiscardCreatedOdstHooks()
    {
        OdstRequestFallback(OdstFallbackReason::InstallFailure);
        if (!DisableAndRemoveOdstHooks())
        {
            // Retain every original, target, and the module reference. A
            // pass-through detour is safer than clearing a trampoline that may
            // still be reachable; the worker retries verified cleanup.
            g_odstCamera.installed.store(true, std::memory_order_release);
            PublishOdstLifecycle();
            return false;
        }
        if (!RestoreOdstNativeWeaponIkBypass())
        {
            g_odstCamera.installed.store(true, std::memory_order_release);
            PublishOdstLifecycle();
            LOG("ODST camera rollback: retaining title module until the "
                "native weapon-IK branch is restored");
            return false;
        }
        if (!RestoreOdstCrosshairClassGate())
        {
            g_odstCamera.installed.store(true, std::memory_order_release);
            PublishOdstLifecycle();
            LOG("ODST camera rollback: retaining title module until the "
                "crosshair class-gate is restored");
            return false;
        }
        RestoreOdstMotionBlurVars();
        ReleaseOdstExposureLock();
        ReleaseOdstFiringOriginHook();
        g_odstCamera.installed.store(false, std::memory_order_release);
        ReleaseOdstModuleReferenceAndClearPointers();
        g_odstCamera.teardownRequested.store(false, std::memory_order_release);
        g_odstCamera.fallbackReason.store(static_cast<int>(OdstFallbackReason::None), std::memory_order_release);
        ClearOdstRuntimePublication();
        return true;
    }

    OdstInstallResult InstallOdstCameraCore(uintptr_t base, size_t size, uint32_t runtimeGeneration)
    {
        if (!runtimeGeneration)
            return OdstInstallResult::Failed;
        HMODULE moduleReference = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(base), &moduleReference) ||
            reinterpret_cast<uintptr_t>(moduleReference) != base)
        {
            LOG("ODST camera preflight: exact title mapping is no longer current");
            return OdstInstallResult::Failed;
        }

        OdstResolvedCameraRuntime resolved{};
        const OdstInstallResult preflight = PreflightOdstCameraRuntime(base, size, resolved);
        if (preflight != OdstInstallResult::Installed)
        {
            return preflight;
        }
        if (g_odstCamera.moduleReference || g_odstCamera.hooks.Count() != 0)
        {
            LOG("ODST camera install: prior hook ownership was not cleared");
            return OdstInstallResult::Failed;
        }

        // The first GetModuleHandleExW above remains an
        // identity-only probe so failed preflight does not pin a title image.
        // Only now - after exact ODST preflight and prior-ownership checks have
        // passed - acquire one real loader reference for the lifetime of our
        // MinHook targets and native byte patches. Reach already uses this same
        // post-proof ownership model. Keeping only the identity handle
        // allows MCC to unmap halo3odst.dll before title-exit restoration and
        // causes the repeated stale weapon-IK cleanup storm seen in headset.
        HMODULE ownedModuleReference = nullptr;
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(base), &ownedModuleReference) ||
            reinterpret_cast<uintptr_t>(ownedModuleReference) != base)
        {
            if (ownedModuleReference)
                FreeLibrary(ownedModuleReference);
            LOG("ODST camera preflight: exact title mapping is no longer current");
            return OdstInstallResult::Failed;
        }

        g_odstCamera.moduleBase = base;
        g_odstCamera.moduleSize = size;
        g_odstCamera.moduleReference = ownedModuleReference;
        g_odstCamera.prepareView = reinterpret_cast<PrepareViewFn>(resolved.prepareView);
        g_odstCamera.buildViewport = reinterpret_cast<BuildViewportFn>(resolved.buildViewport);
        g_odstCamera.buildMatrices = reinterpret_cast<BuildMatricesFn>(resolved.buildMatrices);
        g_odstCamera.fpCameraUpload = reinterpret_cast<FpCameraUploadFn>(resolved.fpCameraUpload);
        g_odstCamera.gunCameraArray = resolved.gunCameraArray;
        g_odstNativePauseFlag.store(reinterpret_cast<uintptr_t>(resolved.nativePauseFlag), std::memory_order_release);
        g_odstCamera.motionBlurVars[0] = {resolved.motionBlurScale, *resolved.motionBlurScale};
        g_odstCamera.motionBlurVars[1] = {resolved.motionBlurMax, *resolved.motionBlurMax};
        g_odstCamera.motionBlurResolved = true;
        g_odstCamera.motionBlurZeroed = false;

        // Resolve every ODST theatre prerequisite before enabling a detour or
        // publishing capability. Both are optional, feature-local proofs:
        // failure leaves the complete immersive camera core unchanged.
        LocateCinematicState(base, size);
        LocateOdstCinematicUserInputState(base, size);
        // The ODST vehicle evidence binding. Optional and read-only -
        // failure logs loudly and leaves every camera behavior untouched.
        ResolveOdstVehicleBinding(base, size);

        struct HookSpec
        {
            const char* name;
            void* target;
            void* detour;
            void** original;
        };
        HookSpec hooks[] = {
            {"camera copy", reinterpret_cast<void*>(resolved.camCopy), reinterpret_cast<void*>(&OdstCamCopyHook),
                reinterpret_cast<void**>(&g_odstCamera.originalCamCopy)},
            {"inner view renderer", reinterpret_cast<void*>(resolved.renderView),
                reinterpret_cast<void*>(&OdstRenderViewHook),
                reinterpret_cast<void**>(&g_odstCamera.originalRenderView)},
            {"FP camera rebuild", reinterpret_cast<void*>(resolved.fpCameraRebuild),
                reinterpret_cast<void*>(&OdstFpCameraRebuildHook),
                reinterpret_cast<void**>(&g_odstCamera.originalFpCameraRebuild)},
            {"FP driver", reinterpret_cast<void*>(resolved.fpDriver), reinterpret_cast<void*>(&OdstFpDriverHook),
                reinterpret_cast<void**>(&g_odstCamera.originalFpDriver)},
            {"camera recoil/shake", reinterpret_cast<void*>(resolved.observerCameraEffect),
                reinterpret_cast<void*>(&OdstObserverCameraEffectHook),
                reinterpret_cast<void**>(&g_odstCamera.originalObserverCameraEffect)},
            {"FP interpolation", reinterpret_cast<void*>(resolved.fpInterpolate),
                reinterpret_cast<void*>(&OdstFpInterpolateWeaponHook), &g_odstCamera.originalFpInterpolate},
            {"FP visible palette", reinterpret_cast<void*>(resolved.fpVisiblePalette),
                reinterpret_cast<void*>(&OdstFpVisiblePaletteWeaponHook), &g_odstCamera.originalFpVisiblePalette},
            {"native CHUD phase primary", reinterpret_cast<void*>(resolved.hudPhasePrimary),
                reinterpret_cast<void*>(&OdstHudPhasePrimaryHook),
                reinterpret_cast<void**>(&g_odstCamera.originalHudPhasePrimary)},
            {"native CHUD phase secondary", reinterpret_cast<void*>(resolved.hudPhaseSecondary),
                reinterpret_cast<void*>(&OdstHudPhaseSecondaryHook),
                reinterpret_cast<void**>(&g_odstCamera.originalHudPhaseSecondary)},
            {"native CHUD target copy", reinterpret_cast<void*>(resolved.hudTargetCopy),
                reinterpret_cast<void*>(&OdstHudTargetCopyHook),
                reinterpret_cast<void**>(&g_odstCamera.originalHudTargetCopy)},
        };
        static_assert(_countof(hooks) == 10);

        for (const HookSpec& hook : hooks)
        {
            const MH_STATUS status = MCCVR_CreateHookChecked(hook.target, hook.detour, hook.original);
            if (status != MH_OK)
            {
                LOG("ODST camera install: MH_CreateHook failed for %s (%d); "
                    "rolling back",
                    hook.name, static_cast<int>(status));
                return DiscardCreatedOdstHooks() ? OdstInstallResult::Failed : OdstInstallResult::CleanupPending;
            }
            g_odstCamera.hooks.Remember(hook.target, *hook.original);
            if (hook.target == reinterpret_cast<void*>(resolved.renderView))
                g_odstCamera.renderHookTarget = hook.target;
        }

        bool queueOk = true;
        for (size_t i = 0; i < g_odstCamera.hooks.Count(); ++i)
        {
            void* const target = g_odstCamera.hooks.At(i);
            if (!target)
                continue;
            const MH_STATUS status = MH_QueueEnableHook(target);
            if (status != MH_OK)
            {
                LOG("ODST camera install: queue-enable failed at %p (%d)", target, static_cast<int>(status));
                queueOk = false;
                break;
            }
        }
        g_odstRuntimeGeneration.store(runtimeGeneration, std::memory_order_release);
        OdstPreparePhysicalCrouchCamera(base, size);
        g_odstLastCamCopyMs.store(0, std::memory_order_release);
        TitleAdapter_ClearHeartbeat(GameTitle::Halo3ODST, runtimeGeneration);
        const MH_STATUS applyStatus = queueOk ? MH_ApplyQueued() : MH_UNKNOWN;
        if (!queueOk || applyStatus != MH_OK)
        {
            LOG("ODST camera install: atomic enable failed (%d); rolling back all hooks",
                static_cast<int>(applyStatus));
            return DiscardCreatedOdstHooks() ? OdstInstallResult::Failed : OdstInstallResult::CleanupPending;
        }
        // OWNERSHIP BEGINS HERE - the instant the core hooks are live, not
        // after the optional feature work below. MH_ApplyQueued has just
        // enabled the ten-hook core, so CamCopy is already publishing a
        // heartbeat while the optional hooks resolve. Stamping the install
        // clock down there instead (and re-zeroing g_odstLastCamCopyMs) threw
        // those heartbeats away and restarted the one-second
        // kOdstCameraStableMs interval from scratch, so ODST paid its whole
        // install cost and THEN a full second: measured 945 ms + 1015 ms =
        // ~1.95 s from resolve to stereo, against Halo 3's ~466 ms tail,
        // which is short precisely because its debounce overlaps its
        // remaining hook installs. Halo 3 parity is the point of this core,
        // so it applies here too.
        //
        // Safe against the watchdog: EvaluateOdstHeartbeat is only consulted
        // by the worker once odstHooked is set from this function's return
        // value, so nothing evaluates this clock until install has finished.
        // Every failure path below still rolls the hooks back through
        // DiscardCreatedOdstHooks, and `installed` stays false until the very
        // end, so a rollback can never leave a live clock behind.
        g_odstCamera.captureFailures.store(0, std::memory_order_release);
        g_odstCamera.sawValidCamera.store(false, std::memory_order_release);
        g_odstCamera.fallbackReason.store(static_cast<int>(OdstFallbackReason::None), std::memory_order_release);
        g_odstCamera.teardownRequested.store(false, std::memory_order_release);
        g_odstCamera.cameraArrayReady.store(true, std::memory_order_release);
        g_odstCamera.installedAtMs.store(GetTickCount64(), std::memory_order_release);
        if (!ApplyOdstNativeWeaponIkBypass(resolved.nativeWeaponIkDecision))
        {
            LOG("ODST camera install: weapon-IK bypass failed; rolling back "
                "the complete ten-hook parity transaction");
            return DiscardCreatedOdstHooks() ? OdstInstallResult::Failed : OdstInstallResult::CleanupPending;
        }

        // Best-effort HUD parity: hook ODST's proven anchor-basis output for the
        // shared vertical slider, hide its native class-2 crosshair, and paint
        // the weapon's authored widget as the floating VR reticle, exactly like
        // Halo 3. Missing or ambiguous title proof leaves native behavior stock;
        // a hook-manager failure after ownership begins rolls back everything.
        const OdstOptionalHookResult heightHook = InstallOdstHudHeight(base, size);
        if (heightHook == OdstOptionalHookResult::CleanupRequired)
        {
            return DiscardCreatedOdstHooks() ? OdstInstallResult::Failed : OdstInstallResult::CleanupPending;
        }
        const OdstOptionalHookResult crosshairHooks = InstallOdstCrosshairHider(base, size);
        if (crosshairHooks == OdstOptionalHookResult::CleanupRequired)
        {
            return DiscardCreatedOdstHooks() ? OdstInstallResult::Failed : OdstInstallResult::CleanupPending;
        }
        // Optional, and installed after the crosshair pair so a missing
        // crosshair proof simply leaves this one a lower slot - the ingress
        // scan walks recorded targets and detour ranges as two independent
        // sets, so an absent optional hook never shifts anything.
        const OdstOptionalHookResult brightnessHook = InstallOdstBrightness(base, size);
        if (brightnessHook == OdstOptionalHookResult::CleanupRequired)
        {
            return DiscardCreatedOdstHooks() ? OdstInstallResult::Failed : OdstInstallResult::CleanupPending;
        }
        if (InstallOdstPodLookFeed(base, size) == OdstOptionalHookResult::CleanupRequired)
        {
            return DiscardCreatedOdstHooks() ? OdstInstallResult::Failed : OdstInstallResult::CleanupPending;
        }
        if (!g_odstCamera.hooks.Full() &&
            InstallLegacyWorldCollision(g_odstWorldCollision, GameTitle::Halo3ODST, base, size, runtimeGeneration,
                reinterpret_cast<void*>(&OdstCollisionResolveDetour), 0x00231EC4, nullptr, false, 0x0022F80C,
                kOdstCollisionVectorSignature))
        {
            g_odstCamera.hooks.Remember(
                g_odstWorldCollision.target, reinterpret_cast<void*>(g_odstWorldCollision.original));
        }

        (void)InstallOdstContactMelee(base, size, runtimeGeneration);
        (void)InstallOdstMuzzle(base, size, runtimeGeneration);
        (void)InstallOdstWeaponHaptics(base, size, runtimeGeneration);

        // The ownership block that used to sit here now runs the moment the
        // core hooks go live, above. In particular g_odstLastCamCopyMs is NOT
        // re-zeroed here: the heartbeats CamCopy published while the optional
        // hooks resolved are real, and discarding them is exactly what forced
        // the one-second stability interval to start over after the install.
        g_odstCamera.installed.store(true, std::memory_order_release);
        g_odstCamera.armed.store(false, std::memory_order_release);
        g_stereoEye.store(-1, std::memory_order_release);
        g_aimSeen.store(false, std::memory_order_release);
        for (auto& valid : g_barrelInWristValid)
            valid.store(false, std::memory_order_release);
        g_camValid.store(false, std::memory_order_release);
        g_baseCamValid.store(false, std::memory_order_release);
        g_zoomFactor.store(1.0f, std::memory_order_release);
        g_enabled.store(false, std::memory_order_release);
        g_positional.store(true, std::memory_order_release);
        g_needRecenter.store(true, std::memory_order_release);
        if (!PublishOdstLifecycle())
        {
            LOG("ODST camera install: title generation changed; requesting verified cleanup");
            OdstRequestFallback(OdstFallbackReason::TitleLeft);
            return OdstInstallResult::CleanupPending;
        }
        TitleAdapter_PublishMode(GameTitle::Halo3ODST, runtimeGeneration, RuntimeMode::Loading);
        VR_SetScopeActive(false);
        // ODST cinematic FOV parity: Halo applies a 25% widescreen
        // FOV reduction while a cinematic is in progress, which also narrows the
        // visibility projection and pulls the view in during ODST cutscenes.
        // Halo 3 name-resolves this engine debug var in InstallHalo3CameraHooks and holds it
        // at 0 while immersive stereo is active, but theatre restores the stock
        // value. ODST installs through this separate core,
        // so resolve it here against halo3odst.dll's own table using the same
        // proven FindDebugVarFloat path already used for the motion-blur vars.
        // UpdateCinematicFovPolicy() enforces it from the ODST present tick, and
        // the pointer is cleared in the hook-state teardown below.
        ResolveCinematicFovVar(base, size);
        LOG("ODST camera install: ten-hook camera/weapon/CHUD core plus %zu "
            "verified optional HUD hook(s) retained and disarmed; "
            "waiting for presentation detach and a fresh camera heartbeat; "
            "Halo 3 weapon, arm IK, two-hand, and dual-wield config path ready",
            g_odstCamera.hooks.Count() - _countof(hooks));
        return OdstInstallResult::Installed;
    }

    bool RemoveOdstCameraCore()
    {
        g_odstCamera.cameraArrayReady.store(false, std::memory_order_release);
        InvalidateHudLayoutProfile(HudLayoutProfile::Halo3ODST);
        OdstRequestPresentationDetach();
        if (!DisableAndRemoveOdstHooks())
        {
            LOG("ODST camera teardown: verified cleanup incomplete; retaining "
                "module, targets, and trampolines for retry");
            return false;
        }
        if (!RestoreOdstNativeWeaponIkBypass())
        {
            LOG("ODST camera teardown: native weapon-IK restore incomplete; "
                "retaining the exact title module for retry");
            return false;
        }
        if (!RestoreOdstCrosshairClassGate())
        {
            LOG("ODST camera teardown: crosshair class-gate restore incomplete; "
                "retaining the exact title module for retry");
            return false;
        }
        RestoreOdstMotionBlurVars();
        ReleaseOdstExposureLock();
        ReleaseOdstFiringOriginHook();

        const auto reason =
            static_cast<OdstFallbackReason>(g_odstCamera.fallbackReason.load(std::memory_order_acquire));
        const char* reasonName = "requested cleanup";
        if (reason == OdstFallbackReason::LevelUnloaded)
            reasonName = "level unload";
        else if (reason == OdstFallbackReason::EyeRedirectUnavailable)
            reasonName = "eye redirect unavailable";
        else if (reason == OdstFallbackReason::UnsupportedCameraMode)
            reasonName = "camera mode outside the proven single-user path";
        else if (reason == OdstFallbackReason::NoCameraHeartbeat)
            reasonName = "camera heartbeat timeout";
        else if (reason == OdstFallbackReason::InstallFailure)
            reasonName = "install rollback";
        else if (reason == OdstFallbackReason::TitleLeft)
            reasonName = "title exit";
        else if (reason == OdstFallbackReason::NativePause)
            reasonName = "native pause boundary";
        else if (reason == OdstFallbackReason::ManualRecovery)
            reasonName = "manual VR recovery";

        g_odstCamera.installed.store(false, std::memory_order_release);
        g_odstLastCamCopyMs.store(0, std::memory_order_release);
        g_stereoEye.store(-1, std::memory_order_release);
        g_aimSeen.store(false, std::memory_order_release);
        for (auto& valid : g_barrelInWristValid)
            valid.store(false, std::memory_order_release);
        g_camValid.store(false, std::memory_order_release);
        g_baseCamValid.store(false, std::memory_order_release);
        g_zoomFactor.store(1.0f, std::memory_order_release);
        g_odstCamera.sawValidCamera.store(false, std::memory_order_release);
        g_odstCamera.captureFailures.store(0, std::memory_order_release);
        ReleaseOdstModuleReferenceAndClearPointers();
        g_odstCamera.teardownRequested.store(false, std::memory_order_release);
        g_odstCamera.fallbackReason.store(static_cast<int>(OdstFallbackReason::None), std::memory_order_release);
        ClearOdstRuntimePublication();
        LOG("ODST camera teardown complete (%s); stock renderer owns the title", reasonName);
        return true;
    }

    bool ProbeOdstCameraReadiness(const wchar_t* moduleName, uintptr_t cameraArrayRva)
    {
        if (!moduleName || !cameraArrayRva)
            return false;
        uintptr_t base = 0;
        size_t size = 0;
        if (!sig::ModuleRange(moduleName, base, size))
            return false;
        HMODULE moduleReference = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(base), &moduleReference) ||
            reinterpret_cast<uintptr_t>(moduleReference) != base)
        {
            return false;
        }

        uintptr_t textBegin = 0, textEnd = 0;
        uint32_t timestamp = 0;
        size_t imageSize = 0;
        const size_t arraySize = 4 * kOdstCameraProfile.layout.viewStride;
        const bool identityMatches = ResolveOdstTextRange(base, size, textBegin, textEnd, timestamp, imageSize) &&
                                     timestamp == kOdstCameraProfile.expectedTimestamp &&
                                     imageSize == kOdstCameraProfile.expectedImageSize && imageSize == size;
        const bool arrayFits = cameraArrayRva < size && arraySize <= size - cameraArrayRva;
        const bool ready = identityMatches && arrayFits && OdstCameraArraySupportsBringup(base + cameraArrayRva);
        return ready;
    }
#include "odst_contact_melee_runtime.inl"
#include "odst_physical_crouch.inl"
#include "odst_muzzle_ownership.inl"
#include "odst_melee_selection.inl" // after OdstMuzzleObject / OdstOwnedWeapon
#include "odst_muzzle_publication.inl"
#include "odst_muzzle_shots.inl"
#include "odst_muzzle_lifecycle.inl"
#include "odst_weapon_haptics.inl"
#include "odst_finger_pose.inl"
    bool DisableAndRemoveOdstContactMelee()
    {
        g_odstContact.enabled.store(false, std::memory_order_release);
        if (!RemoveOdstMeleeSelection())
            return false;                                // as Halo 3
        return LegacyContactRetire<OdstContactTraits>(); // as Halo 3
    }
} // namespace
namespace odst_bridge
{
    // ODST presentation priming, pause/unload/heartbeat fallbacks, teardown and rearm gates.
    // Returns odstAvailableForInstall.
    bool TickOdstLifecycle(const TitleTickContext& c)
    {
        const TitleDescriptor* activeTitle = c.activeTitle;
        (void)activeTitle;
        const bool activeLevelRunning = c.activeLevelRunning;
        (void)activeLevelRunning;
        const uint64_t pollNow = c.now;
        (void)pollNow;
        const TitleAdapterRuntimeSnapshot& runtime = *c.runtime;
        const TitleHookPlan hookPlan = c.hookPlan;
        bool& hookRefreshPending = g_supervisorTitles.hookRefreshPending;
        (void)hookRefreshPending;
        bool& odstHooked = g_supervisorTitles.odstHooked;
        bool& odstAttempted = g_supervisorTitles.odstAttempted;
        uintptr_t& odstCameraArrayRva = g_supervisorTitles.odstCameraArrayRva;
        uint64_t& odstNextAttemptMs = g_supervisorTitles.odstNextAttemptMs;
        OdstCameraRearmGate& odstRearmGate = g_supervisorTitles.odstRearmGate;
        OdstPauseRearmGate& odstPauseRearmGate = g_supervisorTitles.odstPauseRearmGate;
        bool& odstPresentationPrepared = g_supervisorTitles.odstPresentationPrepared;
        (void)odstPresentationPrepared;
        bool& odstManualRecoveryPending = g_supervisorTitles.odstManualRecoveryPending;
        (void)odstManualRecoveryPending;
        const bool odstAvailableForInstall = hookPlan == TitleHookPlan::OdstExperimentalCameraCore;
        const uint32_t odstGeneration = g_odstRuntimeGeneration.load(std::memory_order_acquire);
        const uint32_t observedOdstGeneration = TitleAdapter_GetGeneration(GameTitle::Halo3ODST);
        const bool odstGenerationMismatch = odstHooked && (!odstGeneration || odstGeneration != observedOdstGeneration);
        const bool odstRuntimeRetained = odstHooked && odstGeneration &&
                                         runtime.runtime.owner == GameTitle::Halo3ODST &&
                                         runtime.runtime.generation == odstGeneration &&
                                         (runtime.runtime.qualifyingOwnerCount == 1 || runtime.ownershipPending);
        const bool odstActive = !odstGenerationMismatch && (odstAvailableForInstall || odstRuntimeRetained);
        if (odstActive && !odstPresentationPrepared)
        {
            // Prime the render-thread detach while ODST is still loading.
            // The proven Halo 3 one-second camera debounce can then begin
            // immediately when the camera hooks publish their heartbeat.
            OdstRequestPresentationDetach();
            odstPresentationPrepared = true;
            LOG("ODST camera presentation: detach primed during title load");
        }
        else if (!odstActive)
            odstPresentationPrepared = false;
        bool odstPaused = false;
        const bool odstPauseKnown = odstActive && ReadOdstEnginePaused(odstPaused);
        if (odstHooked && odstPauseKnown && odstPaused &&
            !g_odstCamera.teardownRequested.load(std::memory_order_acquire))
        {
            LOG("ODST pause boundary: native pause entered; removing "
                "private camera hooks before any Save & Quit teardown");
            OdstRequestFallback(OdstFallbackReason::NativePause);
        }
        if (odstHooked && odstActive && !activeLevelRunning &&
            !g_odstCamera.teardownRequested.load(std::memory_order_acquire))
        {
            LOG("ODST level-liveness boundary: retiring hooks before MCC "
                "can unload the title DLL");
            OdstRequestFallback(OdstFallbackReason::LevelUnloaded);
        }
        if (odstHooked && odstActive && !g_odstCamera.teardownRequested.load(std::memory_order_acquire))
        {
            const uint64_t now = GetTickCount64();
            const uint64_t installedAt = g_odstCamera.installedAtMs.load(std::memory_order_acquire);
            const uint64_t last = g_odstLastCamCopyMs.load(std::memory_order_acquire);
            const bool sawCamera = g_odstCamera.sawValidCamera.load(std::memory_order_acquire);
            const bool cameraReady = OdstCameraArraySupportsBringup(g_odstCamera.gunCameraArray);
            g_odstCamera.cameraArrayReady.store(cameraReady, std::memory_order_release);
            if (!g_odstCamera.armed.load(std::memory_order_acquire))
                LogOdstWaitingReadinessIfChanged(g_odstCamera.gunCameraArray);
            const OdstHeartbeatAction heartbeat = EvaluateOdstHeartbeat(now, installedAt, last, sawCamera, cameraReady);
            if (heartbeat == OdstHeartbeatAction::LevelUnloaded)
                OdstRequestFallback(OdstFallbackReason::LevelUnloaded);
            else if (heartbeat == OdstHeartbeatAction::NoFirstHeartbeat)
                OdstRequestFallback(OdstFallbackReason::NoCameraHeartbeat);
        }
        const bool odstTeardown = g_odstCamera.teardownRequested.load(std::memory_order_acquire);
        if (odstHooked && (!odstActive || odstTeardown))
        {
            auto reason = static_cast<OdstFallbackReason>(g_odstCamera.fallbackReason.load(std::memory_order_acquire));
            if (!odstActive && reason == OdstFallbackReason::None)
            {
                OdstRequestFallback(OdstFallbackReason::TitleLeft);
                reason = OdstFallbackReason::TitleLeft;
            }
            // Cross-title diagnostic (read-only): when ODST is torn down
            // because the title poll went ambiguous/Unknown (another Halo
            // module is merely resident) rather than an explicit pause/unload,
            // record whether ODST's OWN camera was still rendering at that
            // instant. A fresh heartbeat proves the teardown is premature --
            // ODST owns the frame and its hooks should be retained instead.
            if (!odstActive)
            {
                const uint64_t lastCam = g_odstLastCamCopyMs.load(std::memory_order_acquire);
                const uint64_t age = (lastCam && pollNow >= lastCam) ? pollNow - lastCam : UINT64_MAX;
                LOG("ODST cross-title diag: title poll ambiguous/Unknown, "
                    "tearing down (reason=%d) armed=%d; ODST camera heartbeat "
                    "age=%llu ms -- retention candidate = %s",
                    static_cast<int>(reason), g_odstCamera.armed.load(std::memory_order_acquire) ? 1 : 0,
                    static_cast<unsigned long long>(age),
                    (age <= 300) ? "YES (ODST still rendering -- premature teardown)"
                                 : "NO (ODST heartbeat stale -- genuine title exit)");
            }
            const bool cameraReadyBeforeRemoval = !odstGenerationMismatch && g_odstCamera.gunCameraArray &&
                                                  OdstCameraArraySupportsBringup(g_odstCamera.gunCameraArray);
            if (RemoveOdstCameraCore())
            {
                odstHooked = false;
                // The next install must prove the level is running again.
                g_odstLevelLoadGate.Rearm();
                if (reason == OdstFallbackReason::UnsupportedCameraMode)
                {
                    // A menu is a temporary camera mode, not the end of the
                    // session. Blocking until title exit meant opening
                    // ODST's menu once permanently killed VR: presentation
                    // still flipped back to stereo when the menu closed,
                    // but the camera core stayed torn down behind it, so
                    // the player was left in a hollow 3D that never
                    // recovered without leaving the title.
                    //
                    // The original concern - that a menu can briefly
                    // resemble an unload/reload and must not be reinstalled
                    // behind - is handled by the same gate level
                    // transitions already use. It requires the camera to be
                    // observed NOT ready and then ready again, which only
                    // happens once the menu is gone and gameplay has
                    // resumed, so the core cannot come back while the menu
                    // is still up.
                    odstRearmGate.BlockUntilReload(cameraReadyBeforeRemoval);
                    odstAttempted = true;
                    LOG("ODST camera rearm blocked until gameplay resumes "
                        "after unsupported/menu camera mode");
                }
                else if (reason == OdstFallbackReason::NativePause)
                {
                    odstPauseRearmGate.Block();
                    odstAttempted = true;
                    LOG("ODST camera rearm blocked until native pause exits "
                        "and the live camera is stable");
                }
                else if (reason == OdstFallbackReason::LevelUnloaded)
                {
                    odstRearmGate.BlockUntilReload(cameraReadyBeforeRemoval);
                    odstAttempted = true;
                }
                else if (!odstActive)
                    odstAttempted = false;
                else
                    odstAttempted = true;
            }
        }
        if (odstManualRecoveryPending && !odstHooked)
        {
            // A manual retry releases old attempt/readiness latches only
            // after every installed callback has safely retired. Native
            // pause and the ordinary level-load gate still control entry.
            odstManualRecoveryPending = false;
            odstRearmGate = {};
            odstPauseRearmGate = {};
            odstAttempted = false;
            odstNextAttemptMs = 0;
            ClearOdstStaticPreflightCache();
            g_odstLevelLoadGate.Rearm();
        }
        if (!odstActive && !odstHooked)
        {
            // Re-arm on TITLE EXIT, not only on teardown. Rearm() used to
            // run only where hooks came out, so an exit with nothing
            // installed - the menu churn between two loads - left the
            // gate holding the stillness it had observed IN THE MENU.
            // The next level's very first camera tick then satisfied
            // "frozen, then ticking" at the START of its loading screen,
            // so the gate opened early and ODST's own readiness probe was
            // left to wait out the rest of the load ALONE: measured
            // 07:15:17.078 gate open -> 07:15:24.912 probe satisfied,
            // 7.1 s of pure serialisation on top of an 11.7 s gate hold.
            // With the gate re-armed here the two waits observe the same
            // loading screen concurrently and the cost is the longer of
            // them, not their sum.
            g_odstLevelLoadGate.Rearm();
            odstRearmGate.Observe(false, false);
            odstPauseRearmGate.Observe(pollNow, false, false, false);
            g_odstNativePauseFlag.store(0, std::memory_order_release);
            odstAttempted = false;
            odstCameraArrayRva = 0;
            odstNextAttemptMs = 0;
            g_odstCamera.waitingLogged.store(false, std::memory_order_release);
            g_odstCamera.cameraArrayReady.store(false, std::memory_order_release);
            ClearOdstStaticPreflightCache();
        }
        else if (odstAvailableForInstall && !odstHooked &&
                 (odstRearmGate.IsBlocked() || odstPauseRearmGate.IsBlocked()))
        {
            const bool ready = ProbeOdstCameraReadiness(activeTitle->moduleName, odstCameraArrayRva);
            odstRearmGate.Observe(true, ready);
            odstPauseRearmGate.Observe(pollNow, true, odstPauseKnown && odstPaused, ready);
            if (odstRearmGate.CanAttemptInstall() && odstPauseRearmGate.CanAttemptInstall())
            {
                odstAttempted = false;
                odstNextAttemptMs = 0;
                LOG("ODST verified rearm boundary observed; static preflight may retry");
            }
        }
        return odstAvailableForInstall;
    }
    // Installs the ODST camera core when its rearm gates allow an attempt.
    void TryInstallOdst(const TitleTickContext& c, bool odstAvailableForInstall)
    {
        const TitleDescriptor* activeTitle = c.activeTitle;
        (void)activeTitle;
        const bool activeLevelRunning = c.activeLevelRunning;
        (void)activeLevelRunning;
        const uint64_t pollNow = c.now;
        (void)pollNow;
        bool& gameHooked = g_supervisorTitles.gameHooked;
        bool& hookRefreshPending = g_supervisorTitles.hookRefreshPending;
        (void)hookRefreshPending;
        bool& odstHooked = g_supervisorTitles.odstHooked;
        bool& odstAttempted = g_supervisorTitles.odstAttempted;
        uintptr_t& odstCameraArrayRva = g_supervisorTitles.odstCameraArrayRva;
        uint64_t& odstNextAttemptMs = g_supervisorTitles.odstNextAttemptMs;
        OdstCameraRearmGate& odstRearmGate = g_supervisorTitles.odstRearmGate;
        OdstPauseRearmGate& odstPauseRearmGate = g_supervisorTitles.odstPauseRearmGate;
        bool& odstPresentationPrepared = g_supervisorTitles.odstPresentationPrepared;
        (void)odstPresentationPrepared;
        bool& odstManualRecoveryPending = g_supervisorTitles.odstManualRecoveryPending;
        (void)odstManualRecoveryPending;
        if (!odstHooked && !odstAttempted && odstAvailableForInstall && !gameHooked && !hookRefreshPending &&
            odstRearmGate.CanAttemptInstall() && odstPauseRearmGate.CanAttemptInstall() && pollNow >= odstNextAttemptMs)
        {
            uintptr_t base = 0;
            size_t size = 0;
            // IsOpen(): already sampled once this tick; see the Halo 3 site.
            if (sig::ModuleRange(activeTitle->moduleName, base, size) && g_odstLevelLoadGate.IsOpen())
            {
                const uint32_t generation = TitleAdapter_GetGeneration(GameTitle::Halo3ODST);
                const OdstInstallResult result = InstallOdstCameraCore(base, size, generation);
                if (result == OdstInstallResult::Installed)
                {
                    odstHooked = true;
                    odstAttempted = true;
                    odstCameraArrayRva = g_odstCamera.gunCameraArray - base;
                }
                else if (result == OdstInstallResult::CleanupPending)
                {
                    odstHooked = true;
                    odstAttempted = true;
                    if (g_odstCamera.gunCameraArray >= base)
                        odstCameraArrayRva = g_odstCamera.gunCameraArray - base;
                    LOG("ODST camera install rollback is pending verified cleanup");
                }
                else if (result == OdstInstallResult::Failed)
                {
                    odstAttempted = true;
                    LOG("ODST camera bring-up blocked for this title session; "
                        "the stock renderer remains active");
                }
                else
                {
                    odstNextAttemptMs = pollNow + 500;
                }
            }
        }
    }
    // ODST camera-only bring-up presentation.
    // Returns true when this title owned the tick (Game_AutoVrTick returns immediately).
    bool AutoVrTickOdst()
    {
        static OdstFreshCameraDebounce odstFreshDebounce;
        static bool wasOdstCameraContext = false;
        static bool wasOdstNativePause = false;
        static uint64_t lastPresentationDetachCompleted = 0;
        const bool odstCameraContext = Game_IsCameraOnlyBringup();
        if (odstCameraContext)
        {
            if (!wasOdstCameraContext)
            {
                odstFreshDebounce.Reset();
                if (OdstMustClearForeignPause(true, VR_IsPausePresentationTarget(), VR_IsPausePresentation()))
                {
                    VR_RequestPausePresentation(false);
                    LOG("ODST camera presentation: cleared foreign pause/head-lock "
                        "state at title entry");
                }
            }
            wasOdstCameraContext = true;
            // Hold Halo's widescreen cinematic-FOV reduction at 0 only
            // while ODST is immersive, exactly as the Halo 3 present path does;
            // restore stock while theatre owns the cutscene. The policy no-ops until
            // the var is resolved and self-restores when stereo is off; it must run
            // here because the shared call site below is behind
            // the ODST-false Game_AllowsSharedGameplayFeatures() gate.
            UpdateCinematicFovPolicy();
            const uint64_t now = GetTickCount64();
            const uint64_t last = g_odstLastCamCopyMs.load(std::memory_order_acquire);
            const bool cameraReady = g_odstCamera.cameraArrayReady.load(std::memory_order_acquire);
            const bool cameraFresh = cameraReady && last != 0 && now >= last && now - last < kOdstCameraFreshMs;
            const uint64_t installedAt = g_odstCamera.installedAtMs.load(std::memory_order_acquire);
            const bool sawCamera = g_odstCamera.sawValidCamera.load(std::memory_order_acquire);
            const OdstHeartbeatAction heartbeat = EvaluateOdstHeartbeat(now, installedAt, last, sawCamera, cameraReady);
            const bool cameraLost = heartbeat != OdstHeartbeatAction::None;
            const bool inLevelStable = odstFreshDebounce.Update(now, cameraFresh);

            const GameTitle availableTitle = TitleAdapter_GetActiveTitle();
            const TitleAdapterRuntimeSnapshot titleRuntime = RuntimeSnapshot(now);
            const bool odstTitleActive =
                (availableTitle == GameTitle::Halo3ODST || (titleRuntime.runtime.owner == GameTitle::Halo3ODST &&
                                                               titleRuntime.runtime.qualifyingOwnerCount == 1)) &&
                !g_odstCamera.teardownRequested.load(std::memory_order_acquire);
            if (!odstTitleActive)
                VR_SetScopeActive(false); // only when ODST is not active, not every frame
            // Cutscene-facing confirmation for ODST: OdstApplyHeadLook bumps the
            // shared rebase serial at each authored cut. Log the transition here (a
            // cold worker-side path), mirroring the Halo 3 report, so the headset
            // test can see each cut re-align the view.
            {
                static uint32_t odstLoggedCineSerial = 0;
                const uint32_t serial = g_cinematicRebaseSerial.load(std::memory_order_acquire);
                if (serial != odstLoggedCineSerial)
                {
                    odstLoggedCineSerial = serial;
                    const int32_t scene = g_cinematicRebaseScene.load(std::memory_order_relaxed);
                    const int32_t shot = g_cinematicRebaseShot.load(std::memory_order_relaxed);
                    if (scene >= 0 && shot >= 0)
                        LOG("cutscene facing: aligned to scene %d shot %d", scene, shot);
                    else
                        LOG("cutscene facing: aligned to gameplay camera on exit");
                }
            }
            // Halo 3 latches aimSeen once its live camera hook publishes. Keep the
            // same ownership here; camera heartbeat teardown and title transitions
            // already clear it. Per-update clearing caused XInput aim to drop out
            // between ODST camera copies, especially throughout vehicle cameras.

            bool nativePaused = false;
            const bool nativePauseKnown = ReadOdstEnginePaused(nativePaused);
            if (nativePauseKnown && nativePaused && !wasOdstNativePause)
            {
                wasOdstNativePause = true;
                VR_RequestPausePresentation(true);
                LOG("ODST pause presentation: native pause entered, switching to 2D");
            }
            else if ((!nativePauseKnown || !nativePaused) && wasOdstNativePause)
            {
                wasOdstNativePause = false;
                VR_RequestPausePresentation(false);
                LOG("ODST pause presentation: native pause exited, restoring stereo target");
            }

            // Report ODST's live play state through the same shared runtime-mode
            // channel Halo 3 uses, so systems gated on the mode behave identically.
            // Controller vibration is delivered during stable gameplay (matching
            // Halo 3's headset-confirmed rumble) and stops during native pause or
            // while the level is still loading. This mirrors the Halo 3 setter
            // (RuntimeMode::Paused / Gameplay / Loading); ApplyControllerHaptics
            // still multiplies by the universal haptic_intensity, so the shared
            // config/F1 slider tunes ODST rumble strength with no per-title profile.
            if (odstTitleActive)
            {
                // Once ODST has armed stereo on a stable level, keep
                // reporting Gameplay through brief camera-freshness dips instead of
                // dropping to Loading on the fragile per-frame inLevelStable signal.
                // Movement head-relativity (Game_MoveStickIsLocomotion) and controller
                // rumble (ApplyControllerHaptics) are both gated on this mode, while
                // stereo/head-look/aim ride the persistent armed flag -- so the old
                // gate let movement revert to hand-based and rumble cut out while the
                // view kept working. Tie the mode to the same armed/stereo persistence
                // so all three stay in lockstep. Native pause still maps to Paused;
                // genuine heartbeat loss, pause, or teardown disarm within this same
                // block, so the mode still downgrades on real transitions. Before the
                // first arm, armed is false, so this reports Loading.
                const bool odstStereoActive =
                    g_enabled.load(std::memory_order_relaxed) && g_odstCamera.armed.load(std::memory_order_relaxed);
                RuntimeMode odstMode = (nativePauseKnown && nativePaused)
                                           ? RuntimeMode::Paused
                                           : (odstStereoActive ? RuntimeMode::Gameplay : RuntimeMode::Loading);
                // Upgrade gameplay to the vehicle/turret modes, matching Halo
                // 3. Locomotion and rumble are gated on this, so without it an
                // ODST seat would lose head-relative movement and haptics.
                if (odstMode == RuntimeMode::Gameplay && OdstVehicleFpActive())
                {
                    OdstSeatSnapshot seat;
                    if (OdstReadSeatSnapshot(seat) && seat.anchorValid)
                    {
                        odstMode = seat.identity == OdstVehicleId::StationaryTurret ? RuntimeMode::Turret
                                                                                    : RuntimeMode::Vehicle;
                    }
                }
                const uint32_t generation = g_odstRuntimeGeneration.load(std::memory_order_acquire);
                if (generation)
                {
                    TitleAdapter_PublishMode(GameTitle::Halo3ODST, generation, odstMode);
                }
            }

            // Match Halo 3's live HUD-config timing: begin title-owned layout
            // discovery from the first eligible fresh camera heartbeat, without
            // waiting for the one-second stereo arm. The shared writer still
            // verifies ODST's exact adapter anchor before every foreign write.
            if (OdstHudLayoutEligible(odstTitleActive, odstCameraContext,
                    g_odstCamera.installed.load(std::memory_order_acquire), cameraFresh,
                    g_odstCamera.teardownRequested.load(std::memory_order_acquire), nativePauseKnown && nativePaused))
                HudLayoutAutoTick(HudLayoutProfile::Halo3ODST);

            const bool detachedNow = Game_ProcessPresentationDetachRequest();
            const uint64_t detachCompleted = g_odstCamera.presentationDetachCompleted.load(std::memory_order_acquire);
            const bool observedCompletedDetach = detachCompleted != lastPresentationDetachCompleted;
            lastPresentationDetachCompleted = detachCompleted;
            if (detachedNow || observedCompletedDetach)
            {
                // A completed detach is a hard edge for this Present. Do not let a
                // debounce state retained across a rapid teardown/reinstall re-arm
                // stereo until a full new interval of fresh camera heartbeats.
                odstFreshDebounce.Reset();
                return true;
            }

            if (nativePauseKnown && nativePaused)
            {
                // The worker removes every registered ODST hook at this boundary.
                // Disarm on the
                // render thread immediately so no stereo transaction can begin
                // while pause or Save & Quit advances title teardown.
                g_odstCamera.armed.store(false, std::memory_order_release);
                PublishOdstLifecycle();
                g_enabled.store(false, std::memory_order_release);
                g_autoVrOwned.store(false, std::memory_order_release);
                g_autoVrUserVeto.store(false, std::memory_order_release);
                if (VR_IsStereoEnabled())
                    VR_DetachGamePresentation();
                odstFreshDebounce.Reset();
                return true;
            }

            if (cameraLost && (g_enabled.load(std::memory_order_relaxed) || VR_IsStereoEnabled() ||
                                  g_autoVrOwned.load(std::memory_order_relaxed)))
            {
                LOG("ODST camera presentation: verified heartbeat loss; "
                    "detaching stereo while hook teardown completes");
                g_odstCamera.armed.store(false, std::memory_order_release);
                PublishOdstLifecycle();
                g_enabled.store(false, std::memory_order_release);
                g_autoVrOwned.store(false, std::memory_order_release);
                g_autoVrUserVeto.store(false, std::memory_order_release);
                VR_DetachGamePresentation();
            }

            if (!g_config.auto_vr && !ManualVrRecoveryFor(GameTitle::Halo3ODST))
            {
                if (g_autoVrOwned.load(std::memory_order_acquire))
                {
                    g_odstCamera.armed.store(false, std::memory_order_release);
                    PublishOdstLifecycle();
                    g_enabled.store(false, std::memory_order_release);
                    g_autoVrOwned.store(false, std::memory_order_release);
                    g_autoVrUserVeto.store(false, std::memory_order_release);
                    VR_DetachGamePresentation();
                }
                else
                {
                    const bool wasArmed = g_odstCamera.armed.load(std::memory_order_acquire);
                    const bool eligible =
                        OdstManualArmEligible(inLevelStable, g_enabled.load(std::memory_order_acquire),
                            VR_IsStereoEnabled(), g_odstCamera.teardownRequested.load(std::memory_order_acquire));
                    // Pending may retain a transaction that was already armed,
                    // but it cannot start a new one before the post-transition
                    // camera heartbeat establishes real ownership.
                    g_odstCamera.armed.store(
                        eligible && (wasArmed || (odstTitleActive && !titleRuntime.ownershipPending)),
                        std::memory_order_release);
                    PublishOdstLifecycle();
                }
                return true;
            }
            if (inLevelStable)
            {
                if (odstTitleActive && !titleRuntime.ownershipPending &&
                    !g_autoVrUserVeto.load(std::memory_order_relaxed) &&
                    !g_odstCamera.teardownRequested.load(std::memory_order_acquire) &&
                    (!g_enabled.load(std::memory_order_relaxed) || !VR_IsStereoEnabled() ||
                        !g_odstCamera.armed.load(std::memory_order_relaxed)))
                {
                    g_enabled.store(true, std::memory_order_release);
                    g_needRecenter.store(true, std::memory_order_release);
                    if (!VR_IsStereoEnabled())
                        VR_ToggleStereo();
                    g_autoVrOwned.store(true, std::memory_order_release);
                    g_odstCamera.armed.store(true, std::memory_order_release);
                    PublishOdstLifecycle();
                    LOG("ODST camera bring-up: stable stock camera detected; "
                        "head tracking, stereo, and 6DOF ON");
                }
            }
            else if (cameraLost)
            {
                g_autoVrUserVeto.store(false, std::memory_order_release);
            }
            return true;
        }
        if (wasOdstCameraContext)
        {
            wasOdstCameraContext = false;
            if (wasOdstNativePause)
            {
                wasOdstNativePause = false;
                VR_RequestPausePresentation(false);
            }
            odstFreshDebounce.Reset();
            InvalidateHudLayoutProfile(HudLayoutProfile::Halo3ODST);
        }
        return false;
    }
} // namespace odst_bridge
// The ODST trim table mirrors OdstVehicleId the same way, with the shade
// appended. Same tripwire so the two can never drift apart silently.
static_assert(
    static_cast<int>(OdstVehicleId::Scorpion) == 1 && static_cast<int>(OdstVehicleId::Shade) == kOdstVehicleTrimCount,
    "kOdstVehicleTrimNames must mirror OdstVehicleId order");
namespace odst_bridge
{
    // True while the local player sits in ODST's intro drop pod (a fresh seat
    // sample of this generation; see OdstIsDropPodSeat).
    bool OdstDropPodHeadAimActive()
    {
        if (TitleAdapter_GetActiveTitle() != GameTitle::Halo3ODST)
            return false;
        const uint64_t at = g_odstDropPodSeatMs.load(std::memory_order_acquire);
        const uint32_t generation = g_odstDropPodSeatGeneration.load(std::memory_order_acquire);
        const uint64_t now = GetTickCount64();
        return at && now >= at && now - at <= 500 && generation &&
               generation == g_odstRuntimeGeneration.load(std::memory_order_acquire);
    }
    bool OdstPrimaryWeaponEvidence(uint32_t expectedGeneration, uint32_t& unitOut, uint32_t& weaponOut,
        uint32_t& detailOut, bool* primaryAbsentOut) noexcept
    {
        if (primaryAbsentOut)
            *primaryAbsentOut = false;
        if (!g_odstPlayerUnitGetter || !g_odstUnitInVehicle)
            return false;
        if (g_odstRuntimeGeneration.load(std::memory_order_acquire) != expectedGeneration)
        {
            detailOut = kDiagnosticGuard;
            return false;
        }
        const uint32_t owner = uint32_t(g_odstPlayerUnitGetter(0));
        if (owner == UINT32_MAX || !(owner >> 16))
            return false;
        if (g_odstUnitInVehicle(int32_t(owner)))
            return false;
        uint32_t weapons[2]{};
        if (!OdstReadMuzzleWeapons(owner, weapons, primaryAbsentOut))
            return false;
        if (weapons[0] == UINT32_MAX)
            return false;
        detailOut = (weapons[1] != UINT32_MAX) ? 1u : 0u;
        if (g_odstEngineTlsIndex && *g_odstEngineTlsIndex < 0x200)
        {
            auto** slots = reinterpret_cast<uint8_t**>(__readgsqword(0x58));
            const auto* tls = slots ? slots[*g_odstEngineTlsIndex] : nullptr;
            const auto* users = tls ? *reinterpret_cast<const uint8_t* const*>(tls + 0x598) : nullptr;
            if (users && *reinterpret_cast<const uint32_t*>(users + 0x3C) == weapons[0])
                detailOut |= kDiagnosticFpAgreement;
        }
        if (g_odstRuntimeGeneration.load(std::memory_order_acquire) != expectedGeneration)
        {
            detailOut = kDiagnosticGuard;
            return false;
        }
        unitOut = owner;
        weaponOut = weapons[0];
        return true;
    }
    bool DiagnosticOdstPrimary(
        uint32_t expectedGeneration, uint32_t& unitOut, uint32_t& weaponOut, uint32_t& detailOut) noexcept
    {
        return OdstPrimaryWeaponEvidence(expectedGeneration, unitOut, weaponOut, detailOut, nullptr);
    }
} // namespace odst_bridge

namespace odst_bridge
{
    // Core calls these. An ODST .inl defines them in the anonymous namespace above, beside the ODST code
    // that calls them; each forwarder only forwards.
    void ReportOdstScope()
    {
        return ::ReportOdstScope();
    }
    bool OdstContactMeleeReady()
    {
        return ::OdstContactMeleeReady();
    }
    const uint8_t* OdstContactTls()
    {
        return ::OdstContactTls();
    }
    bool OdstContactObject(uint32_t handle, bool requireBiped)
    {
        return ::OdstContactObject(handle, requireBiped);
    }
    void OdstPublishContactHand(int hand, const FpInterpolationContext& context, const BoneMatrix& root,
        const BoneMatrix* solved, const float points[][3], int count, int32_t unit, uint32_t generation)
    {
        return ::OdstPublishContactHand(hand, context, root, solved, points, count, unit, generation);
    }
    void ReportOdstContactMelee()
    {
        return ::ReportOdstContactMelee();
    }
    bool OdstReadReserveWeapon(uint32_t owner, uint32_t& primary, uint32_t& reserve)
    {
        return ::OdstReadReserveWeapon(owner, primary, reserve);
    }
    void ReportOdstMuzzle()
    {
        return ::ReportOdstMuzzle();
    }
    void ReportOdstWeaponHaptics()
    {
        return ::ReportOdstWeaponHaptics();
    }
    void ReportOdstControllerFingers()
    {
        return ::ReportOdstControllerFingers();
    }
    // Core reads this .inl variable (Game_HasScopeRenderer); the accessor does the same load.
    uint32_t OdstScopeFaultGeneration() noexcept
    {
        return g_odstScopeFaultGeneration.load();
    }
} // namespace odst_bridge
