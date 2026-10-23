// vr_controllers.cpp: controller actions, aim pose, two-hand and support grip, haptics, moved unchanged from vr.cpp. What it provides to the other VR units is
// declared in vr_controllers.h; it reaches their state through vr_internal.h.
#include "../../common/title_traits.h"
#include "../../common/grenade_throw_logic.h"
#include "../../common/holster_logic.h"
#include "../../common/support_diagnostics.h"
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
#include "native_menu_pointer.h"
#include "../../common/game_menu_pointer.h"
#include "../../common/fist_melee_logic.h"
#include "../../common/exclusive_input.h"
#include "../titles/ce/haloce_first_person.h"
#include "../titles/h2/halo2_observer_6dof.h"
#include <../common/authored_reticle_logic.h>
#include "../../common/titles/h4/halo4_world_collision_logic.h"
#include "../../common/input_logic.h"
#include "../../common/zoom_smoothing_logic.h"
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
    constinit XrActionSet g_gameplayActions = XR_NULL_HANDLE;
    constinit XrAction g_rightAimAction = XR_NULL_HANDLE;
    constinit XrSpace g_rightAimSpace = XR_NULL_HANDLE;
    constinit XrAction g_leftAimAction = XR_NULL_HANDLE;
    constinit XrSpace g_leftAimSpace = XR_NULL_HANDLE;
    constinit XrAction g_supportGripPoseAction = XR_NULL_HANDLE;
    constinit XrSpace g_leftGripPoseSpace = XR_NULL_HANDLE;
    constinit XrSpace g_rightGripPoseSpace = XR_NULL_HANDLE;
    constinit XrAction g_actLeftThumbrest = XR_NULL_HANDLE;
    constinit XrAction g_actFacePadL = XR_NULL_HANDLE, g_actFacePadR = XR_NULL_HANDLE;
    constinit XrAction g_actFaceClickL = XR_NULL_HANDLE, g_actFaceClickR = XR_NULL_HANDLE;
} // namespace
namespace vr_core
{
    uint32_t WeaponGestureBinding(GameTitle title, vr_mapping::Action action, uint64_t now)
    {
        const int index = weapon_interaction::TitleIndex(title);
        if (index < 0)
            return 0;
        if (g_config.vr_action_mapping)
            return Game_VrActionTransport(action, now);
        if (action != vr_mapping::Reload && action != vr_mapping::SwitchWeapon)
            return 0; // no legacy key
        return weapon_interaction::Button(
            action == vr_mapping::Reload ? g_config.weapon_reload_button[index] : g_config.weapon_switch_button[index]);
    }
} // namespace vr_core
namespace
{
    // Fist melee: per hand role (0 support, 1 weapon hand), set on the input path.
    constinit std::atomic<bool> g_bareFist[2]{};
    constinit WeaponHapticPulses g_weaponHaptics;
    // Zoomed aim smoothing (time based; shared by every aim caller).
    constinit std::mutex g_zoomSmoothingMutex;
    constinit zoom_smoothing::Smoother g_zoomSmoother;
    // True once the smoother has been reset after a zoom ends, so the common
    // unzoomed aim call skips the lock entirely.
    constinit std::atomic<bool> g_zoomSmootherIdle{true};
    // Protected by g_headCs, or read by the sole pose-publishing frame thread.
    // Optional stock geometry may only combine poses located for the same time.
    constinit XrTime g_stockHeadPoseTime = 0;
    constinit XrTime g_stockControllerPoseTime = 0;
    constinit PhysicalMeleeSpeedHistory g_meleeSpeedHistory[2]{};
    struct ControllerPoseVelocityHistory
    {
        bool valid = false;
        XrTime sampleTime = 0;
        XrVector3f position{};
    };
    constinit ControllerPoseVelocityHistory g_controllerPoseVelocityHistory[2]{};
    struct TwoHandInputSmoothingPublication
    {
        std::atomic<uint32_t> sequence{0};
        std::atomic<uint64_t> serial{0};
        std::atomic<uint64_t> contactSpaceEpoch{0};
        std::atomic<uint32_t> titleGeneration{0};
        std::atomic<uint8_t> title{0};
        std::atomic<uint8_t> leftHanded{0};
        std::atomic<uint8_t> supportEndpointUsedGrip{0};
        std::atomic<uint8_t> active{0};
        std::atomic<float> rawQx{0.0f}, rawQy{0.0f}, rawQz{0.0f}, rawQw{1.0f};
        std::atomic<float> filteredQx{0.0f}, filteredQy{0.0f};
        std::atomic<float> filteredQz{0.0f}, filteredQw{1.0f};
        std::atomic<float> rawPrimaryX{0.0f}, rawPrimaryY{0.0f}, rawPrimaryZ{0.0f};
        std::atomic<float> filteredPrimaryX{0.0f}, filteredPrimaryY{0.0f}, filteredPrimaryZ{0.0f};
        std::atomic<float> rawSupportX{0.0f}, rawSupportY{0.0f}, rawSupportZ{0.0f};
        std::atomic<float> filteredSupportX{0.0f}, filteredSupportY{0.0f}, filteredSupportZ{0.0f};
        std::atomic<float> rawPrimaryGripX{0.0f}, rawPrimaryGripY{0.0f}, rawPrimaryGripZ{0.0f};
        std::atomic<float> filteredPrimaryGripX{0.0f}, filteredPrimaryGripY{0.0f}, filteredPrimaryGripZ{0.0f};
        std::atomic<float> rawSupportGripX{0.0f}, rawSupportGripY{0.0f}, rawSupportGripZ{0.0f};
        std::atomic<float> filteredSupportGripX{0.0f}, filteredSupportGripY{0.0f}, filteredSupportGripZ{0.0f};
        std::atomic<uint8_t> primaryGripValid{0}, supportGripValid{0};
        std::atomic<float> strength{0.0f};
        std::atomic<float> mixedQx{0.0f}, mixedQy{0.0f};
        std::atomic<float> mixedQz{0.0f}, mixedQw{1.0f};
        std::atomic<float> mixedPrimaryX{0.0f}, mixedPrimaryY{0.0f}, mixedPrimaryZ{0.0f};
        std::atomic<float> mixedSupportX{0.0f}, mixedSupportY{0.0f}, mixedSupportZ{0.0f};
        std::atomic<float> mixedPrimaryGripX{0.0f}, mixedPrimaryGripY{0.0f}, mixedPrimaryGripZ{0.0f};
        std::atomic<float> mixedSupportGripX{0.0f}, mixedSupportGripY{0.0f}, mixedSupportGripZ{0.0f};
        std::atomic<float> alpha{0.0f};
        std::atomic<float> orientationErrorDeg{0.0f};
        std::atomic<float> primaryPositionErrorM{0.0f};
        std::atomic<float> supportPositionErrorM{0.0f};
    };
    constinit TwoHandInputSmoothingPublication g_twoHandInputSmoothingPublication;
} // namespace
namespace vr_core
{
    void PublishTwoHandInputSmoothing(uint64_t serial, uint64_t contactSpaceEpoch, GameTitle title,
        uint32_t titleGeneration, bool leftHanded, bool supportEndpointUsedGrip, bool active,
        const two_hand_input_smoothing::Result& result) noexcept
    {
        auto& published = g_twoHandInputSmoothingPublication;
        published.sequence.fetch_add(1, std::memory_order_acq_rel);
        published.serial.store(serial, std::memory_order_relaxed);
        published.contactSpaceEpoch.store(contactSpaceEpoch, std::memory_order_relaxed);
        published.title.store(static_cast<uint8_t>(title), std::memory_order_relaxed);
        published.titleGeneration.store(titleGeneration, std::memory_order_relaxed);
        published.leftHanded.store(leftHanded ? 1u : 0u, std::memory_order_relaxed);
        published.supportEndpointUsedGrip.store(supportEndpointUsedGrip ? 1u : 0u, std::memory_order_relaxed);
        published.active.store(active ? 1u : 0u, std::memory_order_relaxed);
        const auto storeVec = [](auto& x, auto& y, auto& z, virtual_stock::Point3 value)
        {
            x.store(value.x, std::memory_order_relaxed);
            y.store(value.y, std::memory_order_relaxed);
            z.store(value.z, std::memory_order_relaxed);
        };
        published.rawQx.store(result.raw.primaryOrientation.x, std::memory_order_relaxed);
        published.rawQy.store(result.raw.primaryOrientation.y, std::memory_order_relaxed);
        published.rawQz.store(result.raw.primaryOrientation.z, std::memory_order_relaxed);
        published.rawQw.store(result.raw.primaryOrientation.w, std::memory_order_relaxed);
        published.filteredQx.store(result.filtered.primaryOrientation.x, std::memory_order_relaxed);
        published.filteredQy.store(result.filtered.primaryOrientation.y, std::memory_order_relaxed);
        published.filteredQz.store(result.filtered.primaryOrientation.z, std::memory_order_relaxed);
        published.filteredQw.store(result.filtered.primaryOrientation.w, std::memory_order_relaxed);
        storeVec(published.rawPrimaryX, published.rawPrimaryY, published.rawPrimaryZ, result.raw.primaryAimPosition);
        storeVec(published.filteredPrimaryX, published.filteredPrimaryY, published.filteredPrimaryZ,
            result.filtered.primaryAimPosition);
        storeVec(published.rawSupportX, published.rawSupportY, published.rawSupportZ, result.raw.supportAimPosition);
        storeVec(published.filteredSupportX, published.filteredSupportY, published.filteredSupportZ,
            result.filtered.supportAimPosition);
        storeVec(published.rawPrimaryGripX, published.rawPrimaryGripY, published.rawPrimaryGripZ,
            result.raw.primaryGripPosition);
        storeVec(published.filteredPrimaryGripX, published.filteredPrimaryGripY, published.filteredPrimaryGripZ,
            result.filtered.primaryGripPosition);
        storeVec(published.rawSupportGripX, published.rawSupportGripY, published.rawSupportGripZ,
            result.raw.supportGripPosition);
        storeVec(published.filteredSupportGripX, published.filteredSupportGripY, published.filteredSupportGripZ,
            result.filtered.supportGripPosition);
        published.primaryGripValid.store(result.filtered.primaryGripValid ? 1u : 0u, std::memory_order_relaxed);
        published.supportGripValid.store(result.filtered.supportGripValid ? 1u : 0u, std::memory_order_relaxed);
        published.strength.store(result.strength, std::memory_order_relaxed);
        published.mixedQx.store(result.mixed.primaryOrientation.x, std::memory_order_relaxed);
        published.mixedQy.store(result.mixed.primaryOrientation.y, std::memory_order_relaxed);
        published.mixedQz.store(result.mixed.primaryOrientation.z, std::memory_order_relaxed);
        published.mixedQw.store(result.mixed.primaryOrientation.w, std::memory_order_relaxed);
        storeVec(
            published.mixedPrimaryX, published.mixedPrimaryY, published.mixedPrimaryZ, result.mixed.primaryAimPosition);
        storeVec(
            published.mixedSupportX, published.mixedSupportY, published.mixedSupportZ, result.mixed.supportAimPosition);
        storeVec(published.mixedPrimaryGripX, published.mixedPrimaryGripY, published.mixedPrimaryGripZ,
            result.mixed.primaryGripPosition);
        storeVec(published.mixedSupportGripX, published.mixedSupportGripY, published.mixedSupportGripZ,
            result.mixed.supportGripPosition);
        published.alpha.store(result.alpha, std::memory_order_relaxed);
        published.orientationErrorDeg.store(result.primaryOrientationErrorDeg, std::memory_order_relaxed);
        published.primaryPositionErrorM.store(result.primaryPositionErrorM, std::memory_order_relaxed);
        published.supportPositionErrorM.store(result.supportPositionErrorM, std::memory_order_relaxed);
        published.sequence.fetch_add(1, std::memory_order_release);
    }
} // namespace vr_core
namespace
{
    bool ReadTwoHandInputSmoothing(uint64_t expectedSerial, uint64_t expectedContactSpaceEpoch, GameTitle expectedTitle,
        uint32_t expectedGeneration, bool expectedLeftHanded, bool expectedSupportEndpointUsedGrip,
        TwoHandInputSmoothingPreparedOutput& out, bool sampleFrozen = false) noexcept
    {
        out = TwoHandInputSmoothingPreparedOutput{};
        if (!expectedSerial)
            return false;
        auto& published = g_twoHandInputSmoothingPublication;
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const uint32_t before = published.sequence.load(std::memory_order_acquire);
            if (!before || (before & 1u))
                continue;
            TwoHandInputSmoothingPreparedOutput candidate{};
            candidate.serial = published.serial.load(std::memory_order_relaxed);
            const uint64_t contactSpaceEpoch = published.contactSpaceEpoch.load(std::memory_order_relaxed);
            const GameTitle title = static_cast<GameTitle>(published.title.load(std::memory_order_relaxed));
            const uint32_t titleGeneration = published.titleGeneration.load(std::memory_order_relaxed);
            const bool leftHanded = published.leftHanded.load(std::memory_order_relaxed) != 0;
            const bool supportEndpointUsedGrip = published.supportEndpointUsedGrip.load(std::memory_order_relaxed) != 0;
            candidate.active = published.active.load(std::memory_order_relaxed) != 0;
            const auto readVec = [](const auto& x, const auto& y, const auto& z)
            {
                return virtual_stock::Point3{x.load(std::memory_order_relaxed), y.load(std::memory_order_relaxed),
                    z.load(std::memory_order_relaxed)};
            };
            candidate.raw.primaryOrientation = {published.rawQx.load(std::memory_order_relaxed),
                published.rawQy.load(std::memory_order_relaxed), published.rawQz.load(std::memory_order_relaxed),
                published.rawQw.load(std::memory_order_relaxed)};
            candidate.filtered.primaryOrientation = {published.filteredQx.load(std::memory_order_relaxed),
                published.filteredQy.load(std::memory_order_relaxed),
                published.filteredQz.load(std::memory_order_relaxed),
                published.filteredQw.load(std::memory_order_relaxed)};
            candidate.raw.primaryAimPosition =
                readVec(published.rawPrimaryX, published.rawPrimaryY, published.rawPrimaryZ);
            candidate.filtered.primaryAimPosition =
                readVec(published.filteredPrimaryX, published.filteredPrimaryY, published.filteredPrimaryZ);
            candidate.raw.supportAimPosition =
                readVec(published.rawSupportX, published.rawSupportY, published.rawSupportZ);
            candidate.filtered.supportAimPosition =
                readVec(published.filteredSupportX, published.filteredSupportY, published.filteredSupportZ);
            candidate.raw.primaryGripPosition =
                readVec(published.rawPrimaryGripX, published.rawPrimaryGripY, published.rawPrimaryGripZ);
            candidate.filtered.primaryGripPosition =
                readVec(published.filteredPrimaryGripX, published.filteredPrimaryGripY, published.filteredPrimaryGripZ);
            candidate.raw.supportGripPosition =
                readVec(published.rawSupportGripX, published.rawSupportGripY, published.rawSupportGripZ);
            candidate.filtered.supportGripPosition =
                readVec(published.filteredSupportGripX, published.filteredSupportGripY, published.filteredSupportGripZ);
            candidate.raw.primaryGripValid = candidate.filtered.primaryGripValid =
                published.primaryGripValid.load(std::memory_order_relaxed) != 0;
            candidate.raw.supportGripValid = candidate.filtered.supportGripValid =
                published.supportGripValid.load(std::memory_order_relaxed) != 0;
            // The applied blend always carries the raw sample's validity flags:
            // BlendByMix only ever changes positions that are valid at both
            // endpoints, so the mixed sample shares them with raw/filtered.
            candidate.mixed.primaryGripValid = candidate.raw.primaryGripValid;
            candidate.mixed.supportGripValid = candidate.raw.supportGripValid;
            candidate.strength = published.strength.load(std::memory_order_relaxed);
            candidate.mixed.primaryOrientation = {published.mixedQx.load(std::memory_order_relaxed),
                published.mixedQy.load(std::memory_order_relaxed), published.mixedQz.load(std::memory_order_relaxed),
                published.mixedQw.load(std::memory_order_relaxed)};
            candidate.mixed.primaryAimPosition =
                readVec(published.mixedPrimaryX, published.mixedPrimaryY, published.mixedPrimaryZ);
            candidate.mixed.supportAimPosition =
                readVec(published.mixedSupportX, published.mixedSupportY, published.mixedSupportZ);
            candidate.mixed.primaryGripPosition =
                readVec(published.mixedPrimaryGripX, published.mixedPrimaryGripY, published.mixedPrimaryGripZ);
            candidate.mixed.supportGripPosition =
                readVec(published.mixedSupportGripX, published.mixedSupportGripY, published.mixedSupportGripZ);
            candidate.alpha = published.alpha.load(std::memory_order_relaxed);
            candidate.orientationErrorDeg = published.orientationErrorDeg.load(std::memory_order_relaxed);
            candidate.primaryPositionErrorM = published.primaryPositionErrorM.load(std::memory_order_relaxed);
            candidate.supportPositionErrorM = published.supportPositionErrorM.load(std::memory_order_relaxed);
            if (published.sequence.load(std::memory_order_acquire) != before)
                continue;
            const two_hand_input_smoothing::PublicationIdentity expected{expectedSerial, expectedContactSpaceEpoch,
                static_cast<uint8_t>(expectedTitle), expectedGeneration, expectedLeftHanded,
                expectedSupportEndpointUsedGrip};
            const two_hand_input_smoothing::PublicationIdentity actual{candidate.serial, contactSpaceEpoch,
                static_cast<uint8_t>(title), titleGeneration, leftHanded, supportEndpointUsedGrip};
            if (!candidate.active || !two_hand_input_smoothing::SamePublicationIdentity(expected, actual) ||
                contactSpaceEpoch != g_contactSpaceEpoch.load(std::memory_order_acquire) || title != expectedTitle ||
                title != TitleAdapter_GetActiveTitle() || titleGeneration != expectedGeneration ||
                titleGeneration != TitleAdapter_GetGeneration(title) || leftHanded != expectedLeftHanded ||
                supportEndpointUsedGrip != expectedSupportEndpointUsedGrip ||
                !two_hand_input_smoothing::FiniteSample(candidate.raw) ||
                !two_hand_input_smoothing::FiniteSample(candidate.filtered) ||
                !two_hand_input_smoothing::FiniteSample(candidate.mixed) || !(candidate.strength > 0.0f) ||
                candidate.strength > two_hand_input_smoothing::kStrengthMaximum || !std::isfinite(candidate.alpha) ||
                !std::isfinite(candidate.orientationErrorDeg) || !std::isfinite(candidate.primaryPositionErrorM) ||
                !std::isfinite(candidate.supportPositionErrorM) ||
                !committed_aim::PreparedSampleSerialStillMatches(expectedSerial, sampleFrozen,
                    []() noexcept { return g_preparedSerialPublished.load(std::memory_order_acquire); }))
                return false;
            candidate.sampleValid = true;
            out = candidate;
            return true;
        }
        return false;
    }
    // Cross-thread correction read for VR_GetAimPose. The packet carries its
    // own serial, the contact-space epoch and assembly identity it was computed
    // under, and a valid flag that is only set while a correction is active.
    // `expectedSerial` is the prepared serial whose poses the caller solved;
    // a packet from any other serial describes a different live pose, so it
    // fails open instead of presenting a snap-sized correction against the
    // wrong solve. Any inconsistency (torn read, inactive, other serial, epoch
    // change, different solver assembly, non-finite) fails open to identity.
    bool ReadAimContinuityPublishedCorrection(uint64_t expectedSerial, bool& outStockHeadCoherent,
        bool& outSupportEndpointUsedGrip, virtual_stock::Quat4& outCorrection, bool sampleFrozen = false) noexcept
    {
        // Serial zero means no prepared frame has published yet: nothing to
        // match, and the packet must not be trusted.
        if (!expectedSerial)
            return false;
        auto& published = g_aimContinuityPublication;
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const uint32_t before = published.sequence.load(std::memory_order_acquire);
            if (!before || (before & 1u))
                continue;
            const bool valid = published.valid.load(std::memory_order_relaxed) != 0;
            const uint64_t serial = published.serial.load(std::memory_order_relaxed);
            const uint64_t epoch = published.contactSpaceEpoch.load(std::memory_order_relaxed);
            const bool stockHeadCoherent = published.stockHeadCoherent.load(std::memory_order_relaxed) != 0;
            const bool supportEndpointUsedGrip = published.supportEndpointUsedGrip.load(std::memory_order_relaxed) != 0;
            const virtual_stock::Quat4 candidate{published.cx.load(std::memory_order_relaxed),
                published.cy.load(std::memory_order_relaxed), published.cz.load(std::memory_order_relaxed),
                published.cw.load(std::memory_order_relaxed)};
            if (published.sequence.load(std::memory_order_acquire) != before)
                continue;
            if (!valid || !serial)
                return false;
            if (serial != expectedSerial)
                return false;
            if (epoch != g_contactSpaceEpoch.load(std::memory_order_acquire))
                return false;
            virtual_stock::Quat4 normalized{};
            if (!virtual_stock::TryNormalizeQuaternion(candidate, normalized))
                return false;
            // Live solves must still be current after the packet read. A
            // committed solve instead keeps its already-validated sample
            // identity if prepare advances during this call.
            if (!committed_aim::PreparedSampleSerialStillMatches(expectedSerial, sampleFrozen,
                    []() noexcept { return g_preparedSerialPublished.load(std::memory_order_acquire); }))
                return false;
            outStockHeadCoherent = stockHeadCoherent;
            outSupportEndpointUsedGrip = supportEndpointUsedGrip;
            outCorrection = normalized;
            return true;
        }
        return false;
    }

    XrQuaternionf PresentAimContinuityFromPublication(XrQuaternionf orientation, uint64_t expectedSerial,
        bool stockHeadCoherent, bool supportEndpointUsedGrip, bool sampleFrozen = false) noexcept
    {
        bool publishedStockHeadCoherent = false;
        bool publishedSupportEndpointUsedGrip = false;
        virtual_stock::Quat4 correction{};
        if (!ReadAimContinuityPublishedCorrection(
                expectedSerial, publishedStockHeadCoherent, publishedSupportEndpointUsedGrip, correction, sampleFrozen))
            return orientation;
        // The correction is local to the solve it was derived from. A reader
        // whose own solve was assembled differently (no coherent head,
        // or a different support endpoint source) is a different live pose, so
        // it fails open instead of composing a mismatched correction for one
        // frame.
        if (publishedStockHeadCoherent != stockHeadCoherent ||
            publishedSupportEndpointUsedGrip != supportEndpointUsedGrip)
            return orientation;
        return ComposeAimContinuityCorrection(orientation, correction);
    }
} // namespace
namespace vr_core
{
    // Store the head pose for the game camera hook to read. Called once near
    // the end of Present with the NEXT frame's predicted display time, so Halo
    // renders the upcoming image from its matching pose instead of a stale one.
    // stockControllersFresh must be the just-sampled controller/action result
    // for this same prepared frame: success publishes stock coherence under
    // the same lock as the new HMD pose; failure leaves the admission-time
    // invalidation (false) in place so virtual stock falls back to legacy.
    bool CaptureHeadPose(
        XrTime time, bool stockControllersFresh, virtual_stock::InverseNeckNeutralCaptureInput neutralInput)
    {
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        if (XR_FAILED(xrLocateSpace(g_viewSpace, g_localSpace, time, &loc)))
        {
            g_roomscaleHeadSampleMs.store(0, std::memory_order_release);
            return false;
        }
        constexpr XrSpaceLocationFlags need =
            XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
        if ((loc.locationFlags & need) != need)
        {
            g_roomscaleHeadSampleMs.store(0, std::memory_order_release);
            return false;
        }
        if (!NormalizeTrackedPose(loc.pose))
        {
            g_roomscaleHeadSampleMs.store(0, std::memory_order_release);
            return false;
        }
        constexpr XrSpaceLocationFlags physicalTracking =
            XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
        const bool physicallyTracked = (loc.locationFlags & physicalTracking) == physicalTracking;
        EnterCriticalSection(&g_headCs);
        // Filter exactly once per OpenXR frame. CamCopyHook can run several
        // times inside that frame, so smoothing there would compound and vary
        // with Halo's number of camera passes.
        const float smoothing = std::clamp(g_config.headset_smoothing, 0.0f, 0.10f);
        g_headPose =
            g_headPoseValid && smoothing > 0.0f ? SmoothTrackedPose(loc.pose, g_headPose, smoothing) : loc.pose;
        g_headPoseValid = true;
        g_stockHeadPoseTime = time;
        neutralInput.headSuitable = physicallyTracked && g_sessionState == XR_SESSION_STATE_FOCUSED;
        neutralInput.headOrientation = {
            g_headPose.orientation.x, g_headPose.orientation.y, g_headPose.orientation.z, g_headPose.orientation.w};
        virtual_stock::AdvanceInverseNeckNeutralCapture(g_inverseNeckNeutralCapture, neutralInput);
        // Publish stock coherence together with the head pose: an async
        // getter can never observe the new head while freshness still reads
        // false for this same prepared frame.
        g_stockAimFresh.store(stockControllersFresh, std::memory_order_release);
        LeaveCriticalSection(&g_headCs);

        g_roomscaleHeadSampleMs.store(physicallyTracked ? GetTickCount64() : 0, std::memory_order_release);

        // Runtime proof for headset logs: successful pose sampling must equal
        // the OpenXR/game presentation rate. Camera-copy transforms are logged
        // independently in game.cpp and normally exceed this count.
        static uint64_t rateStartMs = 0;
        static unsigned samples = 0;
        ++samples;
        const uint64_t now = GetTickCount64();
        if (!rateStartMs)
            rateStartMs = now;
        else if (now - rateStartMs >= 10000)
        {
            LOG("head pose timing: HMD pose samples %.1f/sec (next-display prediction for each game frame)",
                samples * 1000.0 / (now - rateStartMs));
            samples = 0;
            rateStartMs = now;
        }
        return true;
    }
} // namespace vr_core
namespace
{
    // Gamepad-replacement actions: sticks, buttons, triggers, grips.
    constinit XrAction g_actMove = XR_NULL_HANDLE, g_actTurn = XR_NULL_HANDLE;
    constinit XrAction g_actTrigL = XR_NULL_HANDLE, g_actTrigR = XR_NULL_HANDLE;
    constinit XrAction g_actGripL = XR_NULL_HANDLE, g_actGripR = XR_NULL_HANDLE;
    constinit XrAction g_actA = XR_NULL_HANDLE, g_actB = XR_NULL_HANDLE;
    constinit XrAction g_actX = XR_NULL_HANDLE, g_actY = XR_NULL_HANDLE;
    constinit XrAction g_actClickL = XR_NULL_HANDLE, g_actClickR = XR_NULL_HANDLE;
} // namespace
namespace vr_core
{
    bool CreateControllerActions()
    {
        XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
        strcpy_s(setInfo.actionSetName, "gameplay");
        strcpy_s(setInfo.localizedActionSetName, "MjolnirVR Gameplay");
        setInfo.priority = 0;
        if (XR_FAILED(xrCreateActionSet(g_instance, &setInfo, &g_gameplayActions)))
        {
            LOG("failed to create OpenXR gameplay action set");
            return false;
        }
        if (XR_FAILED(xrStringToPath(g_instance, "/user/hand/right", &g_rightHandPath)) ||
            XR_FAILED(xrStringToPath(g_instance, "/user/hand/left", &g_leftHandPath)))
            return false;

        XrActionCreateInfo actionInfo{XR_TYPE_ACTION_CREATE_INFO};
        actionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
        strcpy_s(actionInfo.actionName, "right_aim_pose");
        strcpy_s(actionInfo.localizedActionName, "Right Hand Aim Pose");
        actionInfo.countSubactionPaths = 1;
        actionInfo.subactionPaths = &g_rightHandPath;
        if (XR_FAILED(xrCreateAction(g_gameplayActions, &actionInfo, &g_rightAimAction)))
        {
            LOG("failed to create right-controller pose action");
            return false;
        }
        strcpy_s(actionInfo.actionName, "left_aim_pose");
        strcpy_s(actionInfo.localizedActionName, "Left Hand Aim Pose");
        actionInfo.subactionPaths = &g_leftHandPath;
        if (XR_FAILED(xrCreateAction(g_gameplayActions, &actionInfo, &g_leftAimAction)))
            g_leftAimAction = XR_NULL_HANDLE; // non-fatal: D-pad gesture falls back to right

        XrPath gripPosePaths[2] = {g_leftHandPath, g_rightHandPath};
        actionInfo.actionType = XR_ACTION_TYPE_POSE_INPUT;
        strcpy_s(actionInfo.actionName, "two_hand_grip_pose");
        strcpy_s(actionInfo.localizedActionName, "Two-Hand Support Grip Pose");
        actionInfo.countSubactionPaths = 2;
        actionInfo.subactionPaths = gripPosePaths;
        if (XR_FAILED(xrCreateAction(g_gameplayActions, &actionInfo, &g_supportGripPoseAction)))
        {
            g_supportGripPoseAction = XR_NULL_HANDLE;
            LOG("Two-hand support grip pose: optional pose action unavailable; aim-pose endpoint retained");
        }

        auto makeAction = [&](XrAction& out, XrActionType type, const char* name, const char* label)
        {
            XrActionCreateInfo ai{XR_TYPE_ACTION_CREATE_INFO};
            ai.actionType = type;
            strcpy_s(ai.actionName, name);
            strcpy_s(ai.localizedActionName, label);
            if (XR_FAILED(xrCreateAction(g_gameplayActions, &ai, &out)))
                out = XR_NULL_HANDLE;
        };
        makeAction(g_actMove, XR_ACTION_TYPE_VECTOR2F_INPUT, "move", "Move (left stick)");
        makeAction(g_actTurn, XR_ACTION_TYPE_VECTOR2F_INPUT, "turn", "Turn (right stick)");
        makeAction(g_actTrigL, XR_ACTION_TYPE_FLOAT_INPUT, "trigger_l", "Left Trigger");
        makeAction(g_actTrigR, XR_ACTION_TYPE_FLOAT_INPUT, "trigger_r", "Right Trigger");
        makeAction(g_actGripL, XR_ACTION_TYPE_FLOAT_INPUT, "grip_l", "Left Grip");
        makeAction(g_actGripR, XR_ACTION_TYPE_FLOAT_INPUT, "grip_r", "Right Grip");
        makeAction(g_actA, XR_ACTION_TYPE_BOOLEAN_INPUT, "btn_a", "A (right lower)");
        makeAction(g_actB, XR_ACTION_TYPE_BOOLEAN_INPUT, "btn_b", "B (right upper)");
        makeAction(g_actX, XR_ACTION_TYPE_BOOLEAN_INPUT, "btn_x", "X (left lower)");
        makeAction(g_actY, XR_ACTION_TYPE_BOOLEAN_INPUT, "btn_y", "Y (left upper)");
        makeAction(g_actClickL, XR_ACTION_TYPE_BOOLEAN_INPUT, "click_l", "Left Stick Click");
        makeAction(g_actClickR, XR_ACTION_TYPE_BOOLEAN_INPUT, "click_r", "Right Stick Click");
        makeAction(g_actMenu, XR_ACTION_TYPE_BOOLEAN_INPUT, "menu", "Menu / Start");
        makeAction(g_actFacePadL, XR_ACTION_TYPE_VECTOR2F_INPUT, "face_pad_l", "Left Touchpad Face Zones");
        makeAction(g_actFacePadR, XR_ACTION_TYPE_VECTOR2F_INPUT, "face_pad_r", "Right Touchpad Face Zones");
        makeAction(g_actFaceClickL, XR_ACTION_TYPE_BOOLEAN_INPUT, "face_click_l", "Left Touchpad Face Click");
        makeAction(g_actFaceClickR, XR_ACTION_TYPE_BOOLEAN_INPUT, "face_click_r", "Right Touchpad Face Click");
        // Always create the optional action so the F1 checkbox works without
        // recreating a session. Failure must not affect existing controller input.
        makeAction(
            g_actLeftThumbrest, XR_ACTION_TYPE_BOOLEAN_INPUT, "left_thumbrest_touch", "Left Thumb Rest Touch (D-pad)");
        if (g_actLeftThumbrest == XR_NULL_HANDLE)
            LOG("D-pad thumb rest: optional touch action unavailable; head gesture and normal input retained");

        XrPath hapticPaths[2] = {g_leftHandPath, g_rightHandPath};
        XrActionCreateInfo hapticInfo{XR_TYPE_ACTION_CREATE_INFO};
        hapticInfo.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT;
        strcpy_s(hapticInfo.actionName, "game_haptics");
        strcpy_s(hapticInfo.localizedActionName, "Game Haptics");
        hapticInfo.countSubactionPaths = 2;
        hapticInfo.subactionPaths = hapticPaths;
        if (XR_FAILED(xrCreateAction(g_gameplayActions, &hapticInfo, &g_hapticAction)))
        {
            g_hapticAction = XR_NULL_HANDLE;
            LOG("portable haptic output unavailable; controller input remains active");
        }

        // Per-profile suggested bindings. SteamVR remaps these onto PSVR2
        // Sense automatically (users can rebind in SteamVR controller
        // settings); the Touch/Index layouts are the closest templates.
        struct Bind
        {
            XrAction action;
            const char* path;
        };
        auto suggest = [&](const char* profile, const Bind* binds, size_t count) -> bool
        {
            XrPath profilePath = XR_NULL_PATH;
            if (XR_FAILED(xrStringToPath(g_instance, profile, &profilePath)))
                return false;
            std::vector<XrActionSuggestedBinding> out;
            for (size_t i = 0; i < count; ++i)
            {
                XrPath p = XR_NULL_PATH;
                if (binds[i].action != XR_NULL_HANDLE && XR_SUCCEEDED(xrStringToPath(g_instance, binds[i].path, &p)))
                    out.push_back({binds[i].action, p});
            }
            if (out.empty())
                return false;
            XrInteractionProfileSuggestedBinding suggestion{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
            suggestion.interactionProfile = profilePath;
            suggestion.suggestedBindings = out.data();
            suggestion.countSuggestedBindings = (uint32_t)out.size();
            return XR_SUCCEEDED(xrSuggestInteractionProfileBindings(g_instance, &suggestion));
        };

        const Bind touch[] = {
            {g_rightAimAction, "/user/hand/right/input/aim/pose"},
            {g_leftAimAction, "/user/hand/left/input/aim/pose"},
            // clang-format off
            {g_actMove,   "/user/hand/left/input/thumbstick"},
            {g_actTurn,   "/user/hand/right/input/thumbstick"},
            {g_actTrigL,  "/user/hand/left/input/trigger/value"},
            {g_actTrigR,  "/user/hand/right/input/trigger/value"},
            {g_actGripL,  "/user/hand/left/input/squeeze/value"},
            {g_actGripR,  "/user/hand/right/input/squeeze/value"},
            {g_actA,      "/user/hand/right/input/a/click"},
            {g_actB,      "/user/hand/right/input/b/click"},
            {g_actX,      "/user/hand/left/input/x/click"},
            {g_actY,      "/user/hand/left/input/y/click"},
            // clang-format on
            {g_actClickL, "/user/hand/left/input/thumbstick/click"},
            {g_actClickR, "/user/hand/right/input/thumbstick/click"},
            // clang-format off
            {g_actMenu,   "/user/hand/left/input/menu/click"},
            {g_actMenu,   "/user/hand/right/input/system/click"},
            // clang-format on
            {g_hapticAction, "/user/hand/left/output/haptic"},
            {g_hapticAction, "/user/hand/right/output/haptic"},
        };
        const Bind index[] = {
            {g_rightAimAction, "/user/hand/right/input/aim/pose"},
            {g_leftAimAction, "/user/hand/left/input/aim/pose"},
            // clang-format off
            {g_actMove,   "/user/hand/left/input/thumbstick"},
            {g_actTurn,   "/user/hand/right/input/thumbstick"},
            {g_actTrigL,  "/user/hand/left/input/trigger/value"},
            {g_actTrigR,  "/user/hand/right/input/trigger/value"},
            {g_actGripL,  "/user/hand/left/input/squeeze/value"},
            {g_actGripR,  "/user/hand/right/input/squeeze/value"},
            {g_actA,      "/user/hand/right/input/a/click"},
            {g_actB,      "/user/hand/right/input/b/click"},
            {g_actX,      "/user/hand/left/input/a/click"},
            {g_actY,      "/user/hand/left/input/b/click"},
            // clang-format on
            {g_actClickL, "/user/hand/left/input/thumbstick/click"},
            {g_actClickR, "/user/hand/right/input/thumbstick/click"},
            {g_actMenu, "/user/hand/left/input/system/click"},
            {g_hapticAction, "/user/hand/left/output/haptic"},
            {g_hapticAction, "/user/hand/right/output/haptic"},
        };
        const Bind wmr[] = {
            {g_actFacePadL, "/user/hand/left/input/trackpad"},
            {g_actFacePadR, "/user/hand/right/input/trackpad"},
            {g_actFaceClickL, "/user/hand/left/input/trackpad/click"},
            {g_actFaceClickR, "/user/hand/right/input/trackpad/click"},
            {g_rightAimAction, "/user/hand/right/input/aim/pose"},
            {g_leftAimAction, "/user/hand/left/input/aim/pose"},
            // clang-format off
            {g_actMove,   "/user/hand/left/input/thumbstick"},
            {g_actTurn,   "/user/hand/right/input/thumbstick"},
            {g_actTrigL,  "/user/hand/left/input/trigger/value"},
            {g_actTrigR,  "/user/hand/right/input/trigger/value"},
            {g_actGripL,  "/user/hand/left/input/squeeze/click"},
            {g_actGripR,  "/user/hand/right/input/squeeze/click"},
            // clang-format on
            {g_actClickL, "/user/hand/left/input/thumbstick/click"},
            {g_actClickR, "/user/hand/right/input/thumbstick/click"},
            {g_actMenu, "/user/hand/left/input/menu/click"},
            {g_hapticAction, "/user/hand/left/output/haptic"},
            {g_hapticAction, "/user/hand/right/output/haptic"},
        };
        const Bind vive[] = {
            {g_actFacePadL, "/user/hand/left/input/trackpad"},
            {g_actFacePadR, "/user/hand/right/input/trackpad"},
            {g_actFaceClickL, "/user/hand/left/input/trackpad/click"},
            {g_actFaceClickR, "/user/hand/right/input/trackpad/click"},
            {g_rightAimAction, "/user/hand/right/input/aim/pose"},
            {g_leftAimAction, "/user/hand/left/input/aim/pose"},
            // clang-format off
            {g_actMove,   "/user/hand/left/input/trackpad"},
            {g_actTurn,   "/user/hand/right/input/trackpad"},
            {g_actTrigL,  "/user/hand/left/input/trigger/value"},
            {g_actTrigR,  "/user/hand/right/input/trigger/value"},
            {g_actGripL,  "/user/hand/left/input/squeeze/click"},
            {g_actGripR,  "/user/hand/right/input/squeeze/click"},
            // clang-format on
            {g_actClickL, "/user/hand/left/input/trackpad/click"},
            {g_actClickR, "/user/hand/right/input/trackpad/click"},
            {g_actMenu, "/user/hand/left/input/menu/click"},
            {g_hapticAction, "/user/hand/left/output/haptic"},
            {g_hapticAction, "/user/hand/right/output/haptic"},
        };
        const Bind simple[] = {
            {g_rightAimAction, "/user/hand/right/input/grip/pose"},
            {g_leftAimAction, "/user/hand/left/input/grip/pose"},
            // clang-format off
            {g_actTrigR,  "/user/hand/right/input/select/click"},
            {g_actMenu,   "/user/hand/left/input/menu/click"},
            // clang-format on
            {g_hapticAction, "/user/hand/left/output/haptic"},
            {g_hapticAction, "/user/hand/right/output/haptic"},
        };
        const Bind gripPoseBindings[] = {
            {g_supportGripPoseAction, "/user/hand/left/input/grip/pose"},
            {g_supportGripPoseAction, "/user/hand/right/input/grip/pose"},
        };
        const auto addGripPoseBindings = [&](std::vector<Bind>& bindings)
        {
            if (g_supportGripPoseAction != XR_NULL_HANDLE)
            {
                bindings.push_back(gripPoseBindings[0]);
                bindings.push_back(gripPoseBindings[1]);
            }
        };
        const auto suggestWithGripFallback = [&](const char* profile, const Bind* original, size_t originalCount)
        {
            if (g_supportGripPoseAction != XR_NULL_HANDLE)
            {
                std::vector<Bind> withGrip(original, original + originalCount);
                addGripPoseBindings(withGrip);
                if (suggest(profile, withGrip.data(), withGrip.size()))
                {
                    LOG("Two-hand support grip pose: optional grip-pose bindings accepted for %s", profile);
                    return true;
                }
                LOG("Two-hand support grip pose: optional grip-pose bindings rejected for %s; retrying complete original controls",
                    profile);
            }
            return suggest(profile, original, originalCount);
        };
        const auto suggestTouch = [&](const char* profile)
        {
            std::vector<Bind> preservedBaseline(std::begin(touch), std::end(touch));
            bool baselineSuggested = false;
            if (g_actLeftThumbrest != XR_NULL_HANDLE)
            {
                std::vector<Bind> withThumbrest = preservedBaseline;
                withThumbrest.push_back({g_actLeftThumbrest, "/user/hand/left/input/thumbrest/touch"});
                if (suggest(profile, withThumbrest.data(), withThumbrest.size()))
                {
                    preservedBaseline = std::move(withThumbrest);
                    baselineSuggested = true;
                    LOG("D-pad thumb rest: optional left touch binding accepted for %s", profile);
                }
                else
                {
                    LOG("D-pad thumb rest: optional binding rejected for %s; preserving complete original controls",
                        profile);
                }
            }
            if (g_supportGripPoseAction == XR_NULL_HANDLE)
                return baselineSuggested || suggest(profile, preservedBaseline.data(), preservedBaseline.size());
            if (g_supportGripPoseAction != XR_NULL_HANDLE)
            {
                std::vector<Bind> withGrip = preservedBaseline;
                addGripPoseBindings(withGrip);
                if (suggest(profile, withGrip.data(), withGrip.size()))
                {
                    LOG("Two-hand support grip pose: optional grip-pose bindings accepted for %s", profile);
                    return true;
                }
                LOG("Two-hand support grip pose: optional grip-pose bindings rejected for %s; restoring preserved baseline",
                    profile);
            }
            // Suggestions replace a profile's complete binding list. Retrying
            // only the new action would discard poses, buttons and haptics. The
            // baseline includes the pre-existing thumb-rest negotiation result.
            return suggest(profile, preservedBaseline.data(), preservedBaseline.size());
        };
        unsigned accepted = 0;
        if (g_touchProProfileEnabled)
            accepted += suggestTouch("/interaction_profiles/facebook/touch_controller_pro");
        accepted += suggestTouch("/interaction_profiles/oculus/touch_controller");
        accepted += suggestWithGripFallback("/interaction_profiles/valve/index_controller", index, _countof(index));
        accepted += suggestWithGripFallback("/interaction_profiles/microsoft/motion_controller", wmr, _countof(wmr));
        accepted += suggestWithGripFallback("/interaction_profiles/htc/vive_controller", vive, _countof(vive));
        accepted += suggestWithGripFallback("/interaction_profiles/khr/simple_controller", simple, _countof(simple));

        XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
        attach.countActionSets = 1;
        attach.actionSets = &g_gameplayActions;
        if (XR_FAILED(xrAttachSessionActionSets(g_session, &attach)))
        {
            LOG("failed to attach OpenXR gameplay action set");
            return false;
        }
        XrActionSpaceCreateInfo spaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        spaceInfo.action = g_rightAimAction;
        spaceInfo.subactionPath = g_rightHandPath;
        spaceInfo.poseInActionSpace.orientation.w = 1.0f;
        if (XR_FAILED(xrCreateActionSpace(g_session, &spaceInfo, &g_rightAimSpace)))
        {
            LOG("failed to create right-controller aim space");
            return false;
        }
        if (g_leftAimAction != XR_NULL_HANDLE)
        {
            spaceInfo.action = g_leftAimAction;
            spaceInfo.subactionPath = g_leftHandPath;
            if (XR_FAILED(xrCreateActionSpace(g_session, &spaceInfo, &g_leftAimSpace)))
                g_leftAimSpace = XR_NULL_HANDLE;
        }
        if (g_supportGripPoseAction != XR_NULL_HANDLE)
        {
            spaceInfo.action = g_supportGripPoseAction;
            spaceInfo.subactionPath = g_leftHandPath;
            if (XR_FAILED(xrCreateActionSpace(g_session, &spaceInfo, &g_leftGripPoseSpace)))
            {
                g_leftGripPoseSpace = XR_NULL_HANDLE;
                LOG("Two-hand support grip pose: optional left grip space unavailable");
            }
            spaceInfo.subactionPath = g_rightHandPath;
            if (XR_FAILED(xrCreateActionSpace(g_session, &spaceInfo, &g_rightGripPoseSpace)))
            {
                g_rightGripPoseSpace = XR_NULL_HANDLE;
                LOG("Two-hand support grip pose: optional right grip space unavailable");
            }
        }
        LOG("right-controller aim action ready (%u interaction profiles accepted)", accepted);
        return true;
    }
} // namespace vr_core
namespace
{
    // Every clear of g_twoHandActive goes through here, so each logged engage
    // is paired 1:1 with a release line (the gesture-check log reader relies on it), whether
    // the release came from the aim solve, a pose outage or the latch.
    void ClearTwoHandActive()
    {
        if (g_twoHandActive.exchange(false, std::memory_order_acq_rel))
            LOG("two-handed aim released");
    }
    constinit grenade_throw::State g_grenadeThrow;
    constinit std::atomic<unsigned> g_weaponGestureGrabs{0}, g_weaponReloadRequests{0}, g_weaponSwapRequests{0};
    constinit std::atomic<unsigned> g_authoredInsertionRequests{0}, g_fallbackInsertionRequests{0};
    constinit std::atomic<uint64_t> g_weaponGestureReadyMs{0};
    // ---- Two-Hand Lab runtime store ----
    //
    // Runtime-only experimental settings: absent from Config, no persistence, and
    // it resets to two_hand_lab::DefaultSettings() (disabled) on process
    // start. The menu UI writes through two_hand_lab_runtime.h;
    // the frame/aim path takes a coherent snapshot without blocking (seqlock
    // with torn-read retry, fail-open to defaults). UI writes (setters) and
    // frame-thread reads (snapshot) never share a lock with the hot aim path.
    //
    // Same-sample coherence proof: the primary/support
    // grip position+validity consumed by the Lab ALWAYS belong to the same
    // controller sample as the AIM poses they solve with, with NO new
    // serial/epoch marker.
    // (1) The sole writer is CaptureRightControllerPose, which publishes the
    // AIM poses, both grip endpoints and both validity flags together under
    // one g_headCs hold on success, located for a single predicted time from
    // one xrSyncActions sample.
    // (2) Grip validity implies same-hand AIM validity from that same capture
    // (g_supportGripPoseValid = leftValid && physical[0],
    // g_primaryGripPoseValid = valid && physical[1]). The grip locates only
    // weaken the AIM gate (position-only vs orientation+position), so the
    // weaker grip gate can never admit a grip whose hand failed AIM: the
    // validity conjunction fails first.
    // (3) Every Lab-relevant assembly reads AIM poses and both grips under
    // one g_headCs hold (VR_GetAimPose) or on the capturing frame thread
    // after it returned (continuity seam, Lab seam, telemetry, presentation
    // snapshots), so no torn cross-sample pair is observable. Handedness
    // swaps the grip endpoints together with the AIM poses under the same
    // hold, preserving the semantic primary/support roles.
    // (4) The early-return paths clear g_primaryGripPoseValid (fail-open) and
    // the support Fresh bit while leaving stale AIM validity behind for the
    // legacy getters. Every grip consumer gates on validity+Fresh (support)
    // or validity+padFresh, the capture's own return for this frame
    // (primary), so a stale grip is never consumed with a newer AIM. The only
    // lock-free grip writes are true->false clears; no valid grip from
    // another sample can be fabricated. g_stockControllerPoseTime and
    // g_stockHeadPoseTime remain writer-only with zero readers. A
    // primary-grip Fresh mirror was considered and rejected: padFresh gates
    // the frame-thread sites and g_headCs gates the getter, which is
    // equivalent with no new state.
    struct TwoHandLabPublication
    {
        std::atomic<uint32_t> sequence{0};
        // Process-wide settings generation (race guard):
        // incremented on every publish, sampled coherently with the settings
        // under the seqlock below. Stamped into AimPoseInputs at assembly and
        // published in the Lab temporal packet so consumers can fail open on
        // a same-serial settings toggle. Never reset (monotonic).
        std::atomic<uint64_t> generation{0};
        std::atomic<uint8_t> enabled{0};
        std::atomic<uint8_t> anchor{0};
        std::atomic<float> offhandInfluence{1.0f};
        std::atomic<uint8_t> agreement{0};
        std::atomic<float> softFullAgreement{0.9f};
        std::atomic<uint8_t> temporal{0};
        std::atomic<float> dampingResponseMs{150.0f};
        std::atomic<float> adaptiveSlowResponseMs{400.0f};
        std::atomic<float> adaptiveFastResponseMs{50.0f};
        std::atomic<float> adaptiveFullErrorDeg{8.0f};
    };
    constinit TwoHandLabPublication g_twoHandLabPublication;
} // namespace
namespace vr_core
{
    two_hand_lab::AnchorMode NormalizeTwoHandLabAnchor(int value) noexcept
    {
        switch (value)
        {
        case 1:
            return two_hand_lab::AnchorMode::AA;
        case 2:
            return two_hand_lab::AnchorMode::AG;
        case 3:
            return two_hand_lab::AnchorMode::GA;
        case 4:
            return two_hand_lab::AnchorMode::GG;
        case 0:
        default:
            return two_hand_lab::AnchorMode::Production;
        }
    }

    two_hand_lab::AgreementMode NormalizeTwoHandLabAgreement(int value) noexcept
    {
        return value == 1 ? two_hand_lab::AgreementMode::SoftAuthority : two_hand_lab::AgreementMode::LegacyHard;
    }

    two_hand_lab::TemporalMode NormalizeTwoHandLabTemporal(int value) noexcept
    {
        switch (value)
        {
        case 1:
            // Product continuity owns the accepted 200 ms latch transition in
            // both Virtual Stock modes. Keep ordinal 1 readable for old
            // runtime-only settings, but never run a duplicate Lab layer.
            return two_hand_lab::TemporalMode::None;
        case 2:
            return two_hand_lab::TemporalMode::ConstantDamping;
        case 3:
            return two_hand_lab::TemporalMode::AdaptiveDamping;
        case 0:
        default:
            return two_hand_lab::TemporalMode::None;
        }
    }

    void PublishTwoHandLabSettings(const two_hand_lab::Settings& settings) noexcept
    {
        const two_hand_lab::Settings replica = two_hand_lab::Normalize(settings);
        auto& published = g_twoHandLabPublication;
        published.sequence.fetch_add(1, std::memory_order_acq_rel);
        published.generation.fetch_add(1, std::memory_order_relaxed);
        published.enabled.store(replica.enabled ? 1u : 0u, std::memory_order_relaxed);
        published.anchor.store(static_cast<uint8_t>(replica.anchor), std::memory_order_relaxed);
        published.offhandInfluence.store(replica.offhandInfluence, std::memory_order_relaxed);
        published.agreement.store(static_cast<uint8_t>(replica.agreement), std::memory_order_relaxed);
        published.softFullAgreement.store(replica.softFullAgreement, std::memory_order_relaxed);
        published.temporal.store(static_cast<uint8_t>(replica.temporal), std::memory_order_relaxed);
        published.dampingResponseMs.store(replica.dampingResponseMs, std::memory_order_relaxed);
        published.adaptiveSlowResponseMs.store(replica.adaptiveSlowResponseMs, std::memory_order_relaxed);
        published.adaptiveFastResponseMs.store(replica.adaptiveFastResponseMs, std::memory_order_relaxed);
        published.adaptiveFullErrorDeg.store(replica.adaptiveFullErrorDeg, std::memory_order_relaxed);
        published.sequence.fetch_add(1, std::memory_order_release);
    }

    // Same-sample snapshot plus the generation of the publish it came from
    // (0 when no publish has completed yet, or on a torn read, which fails
    // open to defaults). CurrentStockAimPoseInputs stamps both into the
    // solve; the temporal packet echoes the generation.
    TwoHandLabSettingsSnapshot SnapshotTwoHandLabSettingsWithGeneration() noexcept
    {
        // Floats are published already normalized; only the enum ordinals
        // need range repair here. A torn read fails open to defaults.
        const two_hand_lab::Settings defaults = two_hand_lab::DefaultSettings();
        auto& published = g_twoHandLabPublication;
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const uint32_t before = published.sequence.load(std::memory_order_acquire);
            if (!before || (before & 1u))
                continue;
            const uint64_t generation = published.generation.load(std::memory_order_relaxed);
            const bool enabled = published.enabled.load(std::memory_order_relaxed) != 0;
            const int anchor = published.anchor.load(std::memory_order_relaxed);
            const float offhandInfluence = published.offhandInfluence.load(std::memory_order_relaxed);
            const int agreement = published.agreement.load(std::memory_order_relaxed);
            const float softFullAgreement = published.softFullAgreement.load(std::memory_order_relaxed);
            const int temporal = published.temporal.load(std::memory_order_relaxed);
            const float dampingResponseMs = published.dampingResponseMs.load(std::memory_order_relaxed);
            const float adaptiveSlowResponseMs = published.adaptiveSlowResponseMs.load(std::memory_order_relaxed);
            const float adaptiveFastResponseMs = published.adaptiveFastResponseMs.load(std::memory_order_relaxed);
            const float adaptiveFullErrorDeg = published.adaptiveFullErrorDeg.load(std::memory_order_relaxed);
            if (published.sequence.load(std::memory_order_acquire) != before)
                continue;
            two_hand_lab::Settings out = defaults;
            out.enabled = enabled;
            out.anchor = NormalizeTwoHandLabAnchor(anchor);
            out.offhandInfluence =
                std::isfinite(offhandInfluence) ? std::clamp(offhandInfluence, 0.0f, 1.0f) : defaults.offhandInfluence;
            out.agreement = NormalizeTwoHandLabAgreement(agreement);
            out.softFullAgreement = std::isfinite(softFullAgreement)
                                        ? std::clamp(softFullAgreement, two_hand_lab::kSoftFullAgreementMinimum,
                                              two_hand_lab::kSoftFullAgreementMaximum)
                                        : defaults.softFullAgreement;
            out.temporal = NormalizeTwoHandLabTemporal(temporal);
            out.dampingResponseMs = dampingResponseMs;
            out.adaptiveSlowResponseMs = adaptiveSlowResponseMs;
            out.adaptiveFastResponseMs = adaptiveFastResponseMs;
            out.adaptiveFullErrorDeg = adaptiveFullErrorDeg;
            TwoHandLabSettingsSnapshot snapshot{};
            snapshot.settings = out;
            snapshot.generation = generation;
            return snapshot;
        }
        return TwoHandLabSettingsSnapshot{};
    }
} // namespace vr_core
namespace
{
    // Freeze this invocation's support qualification BEFORE any support-
    // capable raw aim is selected. PG off short-circuits to the default
    // qualification: no durable read, no forcing, no receipt. An unreadable
    // relationship or an explicitly denied invocation forces the ordinary
    // one-hand path and marks the epoch unknown, so a consumer can never read
    // a denied assembly as a coherently disengaged epoch-0 ray.
    support_grip::SolveSupportQualification ResolveAimSupportQualification() noexcept
    {
        support_grip::SolveSupportQualification qualification{};
        if (!VR_SupportGripWiredForTitle(TitleAdapter_GetActiveTitle()))
            return qualification;
        SupportGripRelationshipSnapshot snapshot{};
        const bool readable = ReadSupportGripRelationshipInternal(snapshot);
        return support_grip::QualifySolveSupport(
            true, readable, snapshot.engaged, snapshot.epoch, g_supportInvocationUntrusted);
    }
} // namespace
namespace vr_core
{
#include "virtual_stock_aim.inl"
    AimPoseInputs CurrentFrameStockAimPoseInputs(
        bool rightValid, const XrPosef& right, bool leftValid, const XrPosef& left) noexcept
    {
        AimPoseInputs inputs = CurrentStockAimPoseInputs(rightValid, right, leftValid, left,
            g_headPoseValid && g_stockAimFresh.load(std::memory_order_acquire), g_headPose.position,
            g_headPose.orientation, g_supportGripPoseValid && g_supportGripPoseFresh.load(std::memory_order_acquire),
            g_supportGripPosePosition,
            // Same-sample primary grip (see the Lab store proof above): the
            // frame thread reads its own capture, so padFresh-equivalent
            // gating (rightValid here) plus validity is sufficient; there is
            // no primary Fresh bit by design.
            rightValid && g_primaryGripPoseValid, g_primaryGripPosePosition);
        ApplyPreparedTwoHandInputSmoothing(inputs, g_preparedFrame.serial);
        return inputs;
    }
    void AdvanceTwoHandInputSmoothingForPreparedFrame(uint64_t preparedSerial, bool padFresh, float dtSeconds) noexcept
    {
        if (!preparedSerial || preparedSerial == g_twoHandInputSmoothingLayer.lastPreparedSerial)
            return;

        TwoHandInputSmoothingLayerState& layer = g_twoHandInputSmoothingLayer;
        const GameTitle title = TitleAdapter_GetActiveTitle();
        const uint32_t generation = TitleAdapter_GetGeneration(title);
        const uint64_t contactSpaceEpoch = g_contactSpaceEpoch.load(std::memory_order_acquire);
        if (layer.haveIdentity &&
            (layer.title != title || layer.generation != generation || layer.contactSpaceEpoch != contactSpaceEpoch))
            two_hand_input_smoothing::Reset(layer.filter);
        layer.title = title;
        layer.generation = generation;
        layer.contactSpaceEpoch = contactSpaceEpoch;
        layer.haveIdentity = true;

        const bool rightFresh = padFresh && g_rightAimPoseValid;
        const bool leftFresh = padFresh && g_leftAimPoseValid;
        const AimPoseInputs inputs = CurrentStockAimPoseInputs(rightFresh, g_rightAimPose, leftFresh, g_leftAimPose,
            g_headPoseValid && g_stockAimFresh.load(std::memory_order_acquire), g_headPose.position,
            g_headPose.orientation, g_supportGripPoseValid && g_supportGripPoseFresh.load(std::memory_order_acquire),
            g_supportGripPosePosition, rightFresh && g_primaryGripPoseValid, g_primaryGripPosePosition);

        if (layer.haveIdentity && layer.leftHanded != inputs.virtualStockLeftHanded)
            two_hand_input_smoothing::Reset(layer.filter);
        layer.leftHanded = inputs.virtualStockLeftHanded;

        // One config read per prepared serial freezes the configured strength for
        // this whole packet: the filter gate, the wet/dry mix, every consumer
        // and telemetry all see the same value.
        //
        // Scope: the filter applies to two-handed aiming with
        // Virtual Stock ON or OFF. The captured poses and the latch sample
        // stay raw (the filter only ever sees copies), and the packet carries
        // no stock-mode identity: a Standard <-> Plus toggle mid-hold keeps
        // the filter's history, which is acceptable for a smoothed input copy
        // and is reported as such (telemetry carries the consumed truth, not
        // the mode that produced it).
        const float strength = two_hand_input_smoothing::ClampStrength(g_config.two_hand_smoothing_strength);
        const bool applicable =
            strength > 0.0f && inputs.twoHandEnabled && inputs.twoHandLatched && inputs.rightValid && inputs.leftValid;
        if (!applicable)
        {
            two_hand_input_smoothing::Reset(layer.filter);
            layer.lastPreparedSerial = preparedSerial;
            // The inactive packet still reports the configured strength so a
            // recording can tell "player chose 25, path not eligible" from "player
            // chose 0". active/advanced stay false, so no geometry is used.
            two_hand_input_smoothing::Result inactive{};
            inactive.strength = strength;
            g_twoHandInputSmoothingOutput = TwoHandInputSmoothingPreparedOutput{};
            g_twoHandInputSmoothingOutput.serial = preparedSerial;
            g_twoHandInputSmoothingOutput.strength = inactive.strength;
            PublishTwoHandInputSmoothing(preparedSerial, contactSpaceEpoch, title, generation,
                inputs.virtualStockLeftHanded, inputs.supportEndpointUsedGrip, false, inactive);
            return;
        }

        two_hand_input_smoothing::Sample raw{};
        raw.primaryOrientation = {inputs.right.orientation.x, inputs.right.orientation.y, inputs.right.orientation.z,
            inputs.right.orientation.w};
        raw.primaryAimPosition = {inputs.right.position.x, inputs.right.position.y, inputs.right.position.z};
        raw.supportAimPosition = {inputs.left.position.x, inputs.left.position.y, inputs.left.position.z};
        raw.primaryGripValid = inputs.primaryGripValid;
        raw.primaryGripPosition = {
            inputs.primaryGripPosition.x, inputs.primaryGripPosition.y, inputs.primaryGripPosition.z};
        raw.supportGripValid = inputs.supportGripValid;
        raw.supportGripPosition = {
            inputs.supportGripPosition.x, inputs.supportGripPosition.y, inputs.supportGripPosition.z};
        const two_hand_input_smoothing::Result result =
            two_hand_input_smoothing::Advance(layer.filter, preparedSerial, dtSeconds, raw, strength);
        layer.lastPreparedSerial = preparedSerial;
        if (result.advanced)
            ++layer.advanceCount;

        TwoHandInputSmoothingPreparedOutput output{};
        output.serial = preparedSerial;
        output.active = result.valid && result.advanced;
        output.sampleValid = result.valid;
        output.strength = result.strength;
        output.raw = result.valid ? result.raw : two_hand_input_smoothing::Sample{};
        output.filtered = result.valid ? result.filtered : two_hand_input_smoothing::Sample{};
        output.mixed = result.valid ? result.mixed : two_hand_input_smoothing::Sample{};
        output.alpha = result.valid ? result.alpha : 0.0f;
        output.orientationErrorDeg = result.valid ? result.primaryOrientationErrorDeg : 0.0f;
        output.primaryPositionErrorM = result.valid ? result.primaryPositionErrorM : 0.0f;
        output.supportPositionErrorM = result.valid ? result.supportPositionErrorM : 0.0f;
        g_twoHandInputSmoothingOutput = output;
        PublishTwoHandInputSmoothing(preparedSerial, contactSpaceEpoch, title, generation,
            inputs.virtualStockLeftHanded, inputs.supportEndpointUsedGrip, output.active, result);
        if (!result.valid)
            two_hand_input_smoothing::Reset(layer.filter);
    }

    // Pure aim calculation shared by the lock-taking public getter and Reach's
    // exact-serial snapshot publisher. It reads no globals, takes no locks, and
    // performs no logging or state publication.
    AimPoseResult ComputeAimPose(const AimPoseInputs& inputs) noexcept
    {
        return ComputeAimPoseImpl<false>(inputs, nullptr);
    }

    AimPoseResult ComputeAimPose(const AimPoseInputs& inputs, AimPoseTrace* trace) noexcept
    {
        return trace ? ComputeAimPoseImpl<true>(inputs, trace) : ComputeAimPoseImpl<false>(inputs, nullptr);
    }
} // namespace vr_core
namespace
{
#include "virtual_stock_aim_continuity_runtime.inl"
#include "two_hand_lab_temporal_runtime.inl"
} // namespace
namespace vr_core
{
    // Live advance seam: exactly once per prepared serial, on the OpenXR frame
    // thread, after input capture (including UpdateTwoHandLatch) and head
    // capture and before anything that must show the corrected aim. It is
    // deliberately outside CapturePreparedFrameTelemetry, so the feature works
    // with recording off.
    //
    // Engagement: g_config.two_handed_aim, for both Virtual Stock on and off.
    // Durable latch acquisition/release is the only transition edge source.
    // A running transition keeps following the live solve regardless of stock
    // mode, menu or configuration changes; tracking/title identity loss still
    // invalidates it conservatively.
    //
    // Switching Plus<->Standard mid-transition deliberately does NOT
    // invalidate: it is a menu-driven geometry change, not a latch edge, and
    // the running ease keeps decaying against the live solve of whichever mode
    // is now selected, so the player still sees a continuous presentation.
    void AdvanceAimContinuityForPreparedFrame(bool padFresh, float dtSeconds) noexcept
    {
        AimContinuityLayerState& layer = g_aimContinuityLayer;
        const GameTitle title = TitleAdapter_GetActiveTitle();
        const uint32_t generation = TitleAdapter_GetGeneration(title);
        const uint64_t contactSpaceEpoch = g_contactSpaceEpoch.load(std::memory_order_acquire);

        // Title/generation and reference-space identity changes invalidate the
        // whole continuity layer even when a transition is active; a new
        // session must never resume a previous anchor, and no filtered
        // two-hand geometry may cross the identity boundary either.
        if (layer.haveIdentity &&
            (layer.title != title || layer.generation != generation || layer.contactSpaceEpoch != contactSpaceEpoch))
            InvalidateAimContinuityLayer();
        layer.title = title;
        layer.generation = generation;
        layer.contactSpaceEpoch = contactSpaceEpoch;
        layer.haveIdentity = true;

        const bool applicable = TwoHandTransitionContinuityAppliesToFrame(g_config.two_handed_aim);
        if (!applicable)
        {
            if (layer.transition.initialized || layer.transition.active || layer.lastPresentedValid ||
                layer.publishedValid || g_aimContinuityOutput.serial)
                InvalidateAimContinuityLayer();
            return;
        }

        // Same-frame stock-aware inputs: the exact assembly the dominant
        // presentation customers use (Reach/Halo 2/Halo 4 render snapshots,
        // the contact-tracking snapshot and the CE rig), i.e. the
        // CurrentFrameStockAimPoseInputs head/support-grip pairing for this
        // prepared serial. VR_GetAimPose assembles the identical values under
        // g_headCs from the same globals.
        const bool rightFresh = padFresh && g_rightAimPoseValid;
        const bool leftFresh = padFresh && g_leftAimPoseValid;
        const AimPoseInputs stockInputs =
            CurrentFrameStockAimPoseInputs(rightFresh, g_rightAimPose, leftFresh, g_leftAimPose);
        // Virtual Stock's existing Standard/Plus continuity remains unchanged.
        // The retired two_hand_transition_smoothing key can no longer disable
        // the VS-OFF 200 ms acquire/release continuity: resolution is fixed-on
        // product behaviour, so this branch is permanently unreachable product
        // code (kept inert rather than deleted).
        if (!stockInputs.virtualStockEnabled && !TwoHandTransitionContinuityEnabled())
        {
            if (layer.transition.initialized || layer.transition.active || layer.lastPresentedValid ||
                layer.publishedValid || g_aimContinuityOutput.serial)
                InvalidateAimContinuityState();
            return;
        }

        const AimContinuityFrameSolve solve = AdvanceAimContinuityFrame(layer.transition, stockInputs,
            g_preparedFrame.serial, dtSeconds, layer.lastPresentedValid, layer.lastPresented);
        if (solve.advanceConsumed)
            ++layer.advanceCount;

        g_aimContinuityOutput.serial = g_preparedFrame.serial;
        g_aimContinuityOutput.liveValid = solve.liveValid;
        g_aimContinuityOutput.live = solve.live;
        g_aimContinuityOutput.oneHandValid = solve.oneHandValid;
        g_aimContinuityOutput.oneHand = solve.oneHand;
        g_aimContinuityOutput.presentedValid = solve.presentedValid;
        g_aimContinuityOutput.presented = solve.presented;

        if (solve.presentedValid)
        {
            layer.lastPresentedValid = true;
            layer.lastPresented = solve.presented;
        }
        const bool publishValid = layer.transition.active && solve.presentedValid;
        layer.publishedValid = publishValid;
        PublishAimContinuityCorrection(g_preparedFrame.serial, contactSpaceEpoch, publishValid, stockInputs.headValid,
            stockInputs.supportEndpointUsedGrip, solve.correction);
    }
} // namespace vr_core
namespace
{
    // Two-Hand Lab frame seam: exactly once per prepared serial,
    // on the OpenXR frame thread, next to the product continuity advance
    // above. VS-off Constant/Adaptive experiments use the product layer's
    // presented orientation as their target when a matching product output
    // exists for the serial, and never own/reseed a second 200 ms latch
    // transition.
    //
    // Engagement (shared predicate TwoHandLabTemporalEngagedFor):
    // stored-enabled AND the frame's own resolved inputs have Virtual Stock
    // off (the Lab never steers Virtual Stock paths) AND two-handed aim is
    // off. The product continuity seam owns every frame whose two-handed aim
    // is enabled, so the Lab temporal stands down there; an engaged frame has
    // no matching product output, so the damping target is the frame's raw
    // stateless Lab orientation. While engaged the seam re-solves those same
    // stock inputs (no new XR sampling) through the temporal fragment, which
    // advances at most once per prepared serial on the prepared-frame dt and
    // publishes the solve's Lab diagnostics for the UI and telemetry.
    // Diagnostics publish for EVERY Lab-applicable frame, including
    // one-hand/exact-A solves (bValid=false there), so the UI shows honest
    // status. Otherwise everything publishes inactive so no stale observation
    // survives, and any held temporal history is dropped. Resets: the mirrored
    // lifecycle invalidations (abort, fatal drain, reference-space change,
    // session/instance loss, handedness swap, title/epoch identity change,
    // inapplicability), plus mode change, Lab enable/disable, VS on/off and
    // two-handed-aim applicability, and Lab Reset (which publishes disabled,
    // so the next serial takes the inactive path). The routine prepared-frame
    // retire deliberately does NOT invalidate, exactly like the VS layer.
    struct TwoHandLabTemporalLayerState
    {
        TwoHandLabTemporalState temporal{};
        // The layer's own last presented orientation, i.e. the fragment
        // output actually emitted for the previous engaged serial. Duplicate
        // serial reads re-present this exact value.
        bool lastPresentedValid = false;
        virtual_stock::Quat4 lastPresented{};
        // Process-lifetime monotonic count of prepared serials that advanced
        // the layer (the fragment's own duplicate-serial and non-positive-dt
        // guards leave it untouched). Telemetry evidence; never reset.
        uint64_t advanceCount = 0;
        GameTitle title = GameTitle::None;
        uint32_t titleGeneration = 0;
        uint64_t contactSpaceEpoch = 0;
        bool haveIdentity = false;
    };
    constinit TwoHandLabTemporalLayerState g_twoHandLabTemporalLayer;
} // namespace
namespace vr_core
{
    // Conservative, deterministic invalidation: no stale history may replay
    // after a reset, epoch change, title/handedness change, feature disable,
    // mode change or applicability change. Consumers see identity afterwards
    // because the packet publishes invalid, which is also what every
    // fail-open path returns. The process-lifetime advanceCount persists, as
    // with the VS layer.
    void InvalidateTwoHandLabTemporal() noexcept
    {
        ResetTwoHandLabTemporal(g_twoHandLabTemporalLayer.temporal);
        g_twoHandLabTemporalLayer.lastPresentedValid = false;
        g_twoHandLabTemporalLayer.lastPresented = virtual_stock::Quat4{};
        g_twoHandLabTemporalLayer.haveIdentity = false;
        g_twoHandLabTemporalOutput = TwoHandLabTemporalPreparedOutput{};
        PublishTwoHandLabTemporalPacket(0, 0, 0, false, virtual_stock::Quat4{}, virtual_stock::Quat4{});
    }
} // namespace vr_core
namespace
{
    // Cross-thread packet read for VR_GetAimPose. Any inconsistency (torn
    // read, inactive, other serial, epoch change, settings generation
    // change, non-finite) fails open to identity.
    bool ReadTwoHandLabTemporalPacket(uint64_t expectedSerial, uint64_t expectedGeneration,
        virtual_stock::Quat4& outStateless, virtual_stock::Quat4& outCorrection, bool sampleFrozen = false) noexcept
    {
        // Serial zero means no prepared frame has published yet: nothing to
        // match, and the packet must not be trusted.
        if (!expectedSerial)
            return false;
        auto& published = g_twoHandLabTemporalPublication;
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const uint32_t before = published.sequence.load(std::memory_order_acquire);
            if (!before || (before & 1u))
                continue;
            const bool valid = published.valid.load(std::memory_order_relaxed) != 0;
            const uint64_t serial = published.serial.load(std::memory_order_relaxed);
            const uint64_t epoch = published.contactSpaceEpoch.load(std::memory_order_relaxed);
            const uint64_t generation = published.generation.load(std::memory_order_relaxed);
            const virtual_stock::Quat4 stateless{published.sx.load(std::memory_order_relaxed),
                published.sy.load(std::memory_order_relaxed), published.sz.load(std::memory_order_relaxed),
                published.sw.load(std::memory_order_relaxed)};
            const virtual_stock::Quat4 correction{published.cx.load(std::memory_order_relaxed),
                published.cy.load(std::memory_order_relaxed), published.cz.load(std::memory_order_relaxed),
                published.cw.load(std::memory_order_relaxed)};
            if (published.sequence.load(std::memory_order_acquire) != before)
                continue;
            if (!valid || !serial)
                return false;
            if (serial != expectedSerial)
                return false;
            if (epoch != g_contactSpaceEpoch.load(std::memory_order_acquire))
                return false;
            if (generation != expectedGeneration)
                return false;
            virtual_stock::Quat4 normalizedStateless{};
            virtual_stock::Quat4 normalizedCorrection{};
            if (!virtual_stock::TryNormalizeQuaternion(stateless, normalizedStateless) ||
                !virtual_stock::TryNormalizeQuaternion(correction, normalizedCorrection))
                return false;
            // Live solves must still be current after the packet read. A
            // committed solve instead keeps its already-validated sample
            // identity if prepare advances during this call.
            if (!committed_aim::PreparedSampleSerialStillMatches(expectedSerial, sampleFrozen,
                    []() noexcept { return g_preparedSerialPublished.load(std::memory_order_acquire); }))
                return false;
            outStateless = normalizedStateless;
            outCorrection = normalizedCorrection;
            return true;
        }
        return false;
    }

    XrQuaternionf PresentTwoHandLabTemporalFromPublication(XrQuaternionf orientation, uint64_t expectedSerial,
        uint64_t expectedGeneration, bool sampleFrozen = false) noexcept
    {
        virtual_stock::Quat4 stateless{};
        virtual_stock::Quat4 correction{};
        if (!ReadTwoHandLabTemporalPacket(expectedSerial, expectedGeneration, stateless, correction, sampleFrozen))
            return orientation;
        // Own-solve match, as on the frame-thread path above.
        virtual_stock::Quat4 consumer{};
        if (!virtual_stock::TryNormalizeQuaternion(
                virtual_stock::Quat4{orientation.x, orientation.y, orientation.z, orientation.w}, consumer) ||
            consumer.x != stateless.x || consumer.y != stateless.y || consumer.z != stateless.z ||
            consumer.w != stateless.w)
            return orientation;
        return ComposeAimContinuityCorrection(orientation, correction);
    }
} // namespace
namespace vr_core
{
    void AdvanceTwoHandLabForPreparedFrame(bool padFresh, float dtSeconds) noexcept
    {
        TwoHandLabTemporalLayerState& layer = g_twoHandLabTemporalLayer;
        const GameTitle title = TitleAdapter_GetActiveTitle();
        const uint32_t titleGeneration = TitleAdapter_GetGeneration(title);
        const uint64_t contactSpaceEpoch = g_contactSpaceEpoch.load(std::memory_order_acquire);

        // Title/generation and reference-space identity changes invalidate
        // the layer even when temporal is engaged; a new session must never
        // resume a previous correction or damping history.
        if (layer.haveIdentity && (layer.title != title || layer.titleGeneration != titleGeneration ||
                                      layer.contactSpaceEpoch != contactSpaceEpoch))
            InvalidateTwoHandLabTemporal();
        layer.title = title;
        layer.titleGeneration = titleGeneration;
        layer.contactSpaceEpoch = contactSpaceEpoch;
        layer.haveIdentity = true;

        const TwoHandLabSettingsSnapshot snap = SnapshotTwoHandLabSettingsWithGeneration();
        // Same-frame stock-aware inputs: the exact assembly the continuity
        // seam uses for this prepared serial. The Lab snapshot (stamped with
        // its generation) and the grip endpoints ride inside
        // CurrentStockAimPoseInputs.
        const bool rightFresh = padFresh && g_rightAimPoseValid;
        const bool leftFresh = padFresh && g_leftAimPoseValid;
        const AimPoseInputs stockInputs =
            CurrentFrameStockAimPoseInputs(rightFresh, g_rightAimPose, leftFresh, g_leftAimPose);
        // Resolved VS-off is required: the Lab never steers Virtual Stock
        // paths, so a VS-on frame publishes invalid diagnostics even when
        // stored-enabled. The temporal presentation resolves through the same
        // shared predicate as its frame-thread mirror (and the product
        // continuity seam), so at most one 200 ms correction is ever active.
        const bool labApplicable = snap.settings.enabled && !stockInputs.virtualStockEnabled;
        const bool temporalEngaged = TwoHandLabTemporalEngagedFor(
            snap.settings.enabled, stockInputs.virtualStockEnabled, g_config.two_handed_aim);
        AimPoseTrace trace{};
        if (!temporalEngaged)
        {
            if (layer.temporal.initialized || layer.lastPresentedValid || g_twoHandLabTemporalOutput.serial)
                InvalidateTwoHandLabTemporal();
            if (labApplicable)
                (void)ComputeAimPose(stockInputs, &trace);
            PublishTwoHandLabDiagnostics(g_preparedFrame.serial, trace.labValid, trace.lab);
            return;
        }

        TwoHandLabProductPresentation product{};
        if (g_aimContinuityOutput.serial == g_preparedFrame.serial)
        {
            product.liveValid = g_aimContinuityOutput.liveValid;
            product.live = g_aimContinuityOutput.live;
            product.presentedValid = g_aimContinuityOutput.presentedValid;
            product.presented = g_aimContinuityOutput.presented;
        }
        const TwoHandLabTemporalFrameSolve solve =
            AdvanceTwoHandLabTemporalFrame(layer.temporal, stockInputs, snap.settings, g_preparedFrame.serial,
                dtSeconds, layer.lastPresentedValid, layer.lastPresented, product, &trace);
        if (solve.advanceConsumed)
            ++layer.advanceCount;

        if (solve.presentedValid)
        {
            layer.lastPresentedValid = true;
            layer.lastPresented = solve.presented;
        }
        virtual_stock::Quat4 correction{};
        const bool correctionValid =
            solve.presentationBaseValid && solve.presentedValid &&
            virtual_stock::TryNormalizeQuaternion(
                virtual_stock::MultiplyQuat4(virtual_stock::ConjugateQuat4(solve.presentationBase), solve.presented),
                correction);
        const bool temporalActive = solve.temporalActive && correctionValid;
        g_twoHandLabTemporalOutput.serial = g_preparedFrame.serial;
        g_twoHandLabTemporalOutput.labGeneration = stockInputs.twoHandLabGeneration;
        g_twoHandLabTemporalOutput.baseValid = solve.presentationBaseValid;
        g_twoHandLabTemporalOutput.base = solve.presentationBase;
        g_twoHandLabTemporalOutput.presentedValid = solve.presentedValid;
        g_twoHandLabTemporalOutput.presented = solve.presented;
        g_twoHandLabTemporalOutput.temporalActive = temporalActive;
        g_twoHandLabTemporalOutput.temporalMode = solve.mode;
        g_twoHandLabTemporalOutput.errorDeg = std::isfinite(solve.errorDeg) ? solve.errorDeg : 0.0f;
        g_twoHandLabTemporalOutput.correction = correctionValid ? correction : virtual_stock::Quat4{};
        PublishTwoHandLabTemporalPacket(g_preparedFrame.serial, contactSpaceEpoch, stockInputs.twoHandLabGeneration,
            temporalActive, solve.presentationBase, g_twoHandLabTemporalOutput.correction);

        // Diagnostics: the frame's stateless Lab observation (trace payload)
        // plus the temporal presentation state. Raw stateless telemetry stays
        // raw; the presented orientation is reported separately via the
        // telemetry family below.
        two_hand_lab::Diagnostics enriched = trace.lab;
        enriched.temporalActive = temporalActive;
        enriched.errorDeg = g_twoHandLabTemporalOutput.errorDeg;
        PublishTwoHandLabDiagnostics(g_preparedFrame.serial, trace.labValid, enriched);
    }
} // namespace vr_core
namespace
{
    // ---- Shots-vs-reticle frame state: presented-aim composition ----
    // Frame-thread counterparts of the VR_GetAimPoseWithSupportProvenance
    // presentation stages for the telemetry capture site. The public getter
    // is forbidden here (takes g_headCs, mutates g_twoHandActive, LOGs); the
    // reads below consume the same lock-free packets on the frame-local solve
    // WITHOUT the g_preparedSerialPublished re-check, which is vacuous on
    // this thread: the seam published for this serial immediately above and
    // no other thread can complete a prepare concurrently. A torn read, or a
    // packet that does not belong to this serial, yields Unknown; only a
    // clean same-serial read yields Identity (no correction for this
    // assembly) or Apply (correction composed exactly as the getter does, via
    // ComposeAimContinuityCorrection, including its fail-open).
    enum class FrameThreadPresentStage : uint8_t
    {
        Unknown,
        Identity,
        Apply
    };
    struct FrameThreadContinuityRead
    {
        FrameThreadPresentStage stage = FrameThreadPresentStage::Unknown;
        virtual_stock::Quat4 correction{};
    };
    FrameThreadContinuityRead ReadContinuityPacketForFrameThread(
        uint64_t expectedSerial, bool stockHeadCoherent, bool supportEndpointUsedGrip) noexcept
    {
        FrameThreadContinuityRead out{};
        if (!expectedSerial)
            return out;
        auto& published = g_aimContinuityPublication;
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const uint32_t before = published.sequence.load(std::memory_order_acquire);
            if (!before || (before & 1u))
                continue;
            const bool valid = published.valid.load(std::memory_order_relaxed) != 0;
            const uint64_t serial = published.serial.load(std::memory_order_relaxed);
            const uint64_t epoch = published.contactSpaceEpoch.load(std::memory_order_relaxed);
            const bool packetHead = published.stockHeadCoherent.load(std::memory_order_relaxed) != 0;
            const bool packetGrip = published.supportEndpointUsedGrip.load(std::memory_order_relaxed) != 0;
            const virtual_stock::Quat4 correction{published.cx.load(std::memory_order_relaxed),
                published.cy.load(std::memory_order_relaxed), published.cz.load(std::memory_order_relaxed),
                published.cw.load(std::memory_order_relaxed)};
            if (published.sequence.load(std::memory_order_acquire) != before)
                continue;
            // A packet from any other serial (including the serial-0
            // invalidated packet) describes a different live pose, so the
            // stage cannot be proven from the packet. The caller falls back
            // to the seam's own engagement state (same rule as the getter's
            // serial gate, evaluated fresh on this thread).
            if (!serial || serial != expectedSerial || epoch != g_contactSpaceEpoch.load(std::memory_order_acquire))
                return out;
            // The correction is local to the live pose it was derived from
            // (same rule as the getter): a differently assembled frame-local
            // solve must not compose it, and identity would misreport an
            // active correction, so this is Unknown rather than Identity.
            if (packetHead != stockHeadCoherent || packetGrip != supportEndpointUsedGrip)
                return out;
            if (!valid)
            {
                out.stage = FrameThreadPresentStage::Identity;
                return out;
            }
            out.stage = FrameThreadPresentStage::Apply;
            out.correction = correction;
            return out;
        }
        return out;
    }

    struct FrameThreadLabRead
    {
        FrameThreadPresentStage stage = FrameThreadPresentStage::Unknown;
        virtual_stock::Quat4 stateless{};
        virtual_stock::Quat4 correction{};
    };
    FrameThreadLabRead ReadLabPacketForFrameThread(uint64_t expectedSerial, uint64_t expectedGeneration) noexcept
    {
        FrameThreadLabRead out{};
        if (!expectedSerial)
            return out;
        auto& published = g_twoHandLabTemporalPublication;
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const uint32_t before = published.sequence.load(std::memory_order_acquire);
            if (!before || (before & 1u))
                continue;
            const bool valid = published.valid.load(std::memory_order_relaxed) != 0;
            const uint64_t serial = published.serial.load(std::memory_order_relaxed);
            const uint64_t epoch = published.contactSpaceEpoch.load(std::memory_order_relaxed);
            const uint64_t generation = published.generation.load(std::memory_order_relaxed);
            const virtual_stock::Quat4 stateless{published.sx.load(std::memory_order_relaxed),
                published.sy.load(std::memory_order_relaxed), published.sz.load(std::memory_order_relaxed),
                published.sw.load(std::memory_order_relaxed)};
            const virtual_stock::Quat4 correction{published.cx.load(std::memory_order_relaxed),
                published.cy.load(std::memory_order_relaxed), published.cz.load(std::memory_order_relaxed),
                published.cw.load(std::memory_order_relaxed)};
            if (published.sequence.load(std::memory_order_acquire) != before)
                continue;
            if (!serial || serial != expectedSerial || epoch != g_contactSpaceEpoch.load(std::memory_order_acquire) ||
                generation != expectedGeneration)
                return out;
            if (!valid)
            {
                out.stage = FrameThreadPresentStage::Identity;
                return out;
            }
            // Normalized here so the caller can exact-match stateless exactly
            // as PresentTwoHandLabTemporalFromPublication does; a
            // non-normalizable packet fails open to Identity in the getter.
            virtual_stock::Quat4 normalizedStateless{};
            virtual_stock::Quat4 normalizedCorrection{};
            if (!virtual_stock::TryNormalizeQuaternion(stateless, normalizedStateless) ||
                !virtual_stock::TryNormalizeQuaternion(correction, normalizedCorrection))
            {
                out.stage = FrameThreadPresentStage::Identity;
                return out;
            }
            out.stage = FrameThreadPresentStage::Apply;
            out.stateless = normalizedStateless;
            out.correction = normalizedCorrection;
            return out;
        }
        return out;
    }

    bool LabTemporalEngagedForFrameThread(bool virtualStockEnabled) noexcept
    {
        const TwoHandLabSettingsSnapshot snap = SnapshotTwoHandLabSettingsWithGeneration();
        return TwoHandLabTemporalEngagedFor(snap.settings.enabled, virtualStockEnabled, g_config.two_handed_aim);
    }
} // namespace
namespace vr_core
{
    // Composes the presented/steering orientation for this frame's own solve,
    // exactly as VR_GetAimPoseWithSupportProvenance does (frame-local solve
    // orientation, then VS continuity presentation, then Lab temporal
    // presentation). Returns false when any stage is not provably the
    // getter's output for this frame's assembly; the caller then records
    // presented_aim_valid=false while the transition_* / two_hand_lab_*
    // piece fields carry the evidence (never faked).
    bool ComposePresentedAimForFrameThread(const XrQuaternionf& liveOrientation, uint64_t preparedSerial,
        bool stockHeadCoherent, bool supportEndpointUsedGrip, uint64_t labGeneration, bool virtualStockEnabled,
        XrQuaternionf& outPresented) noexcept
    {
        XrQuaternionf continuityPresented = liveOrientation;
        const FrameThreadContinuityRead continuity =
            ReadContinuityPacketForFrameThread(preparedSerial, stockHeadCoherent, supportEndpointUsedGrip);
        if (continuity.stage == FrameThreadPresentStage::Apply)
        {
            continuityPresented = ComposeAimContinuityCorrection(liveOrientation, continuity.correction);
        }
        else if (continuity.stage == FrameThreadPresentStage::Unknown)
        {
            // No packet for this serial: identity is provable only when the
            // seam did not run for it (VS engagement off, read fresh on this
            // same thread microseconds after the seam). An engaged seam
            // without its packet is inconsistent: fail open to invalid.
            if (g_config.virtual_stock && g_config.two_handed_aim)
                return false;
        }

        const FrameThreadLabRead lab = ReadLabPacketForFrameThread(preparedSerial, labGeneration);
        if (lab.stage == FrameThreadPresentStage::Identity)
        {
            outPresented = continuityPresented;
            return true;
        }
        if (lab.stage == FrameThreadPresentStage::Unknown)
        {
            if (LabTemporalEngagedForFrameThread(virtualStockEnabled))
                return false;
            outPresented = continuityPresented;
            return true;
        }
        // Apply: the own-solve match, exactly as the getter. A differently
        // assembled solve must not compose this correction (Unknown, never a
        // fabricated presented ray).
        virtual_stock::Quat4 consumer{};
        if (!virtual_stock::TryNormalizeQuaternion(virtual_stock::Quat4{continuityPresented.x, continuityPresented.y,
                                                       continuityPresented.z, continuityPresented.w},
                consumer) ||
            consumer.x != lab.stateless.x || consumer.y != lab.stateless.y || consumer.z != lab.stateless.z ||
            consumer.w != lab.stateless.w)
            return false;
        outPresented = ComposeAimContinuityCorrection(continuityPresented, lab.correction);
        return true;
    }
} // namespace vr_core
namespace
{
    void ProbeCaptureWeaponCE(GameTitle title) noexcept
    {
        const uint64_t beginSeq =
            Telemetry_PublishWeaponEvent(static_cast<uint8_t>(WeaponOrderEventKind::CaptureProbeBegin),
                static_cast<uint8_t>(WeaponOrderEventStatus::NoObservation), static_cast<uint8_t>(title),
                TitleAdapter_GetGeneration(title), g_preparedFrame.serial, UINT32_MAX, UINT32_MAX, 0, 0);
        HaloCELocalPlayerState state{};
        const bool readOk = HaloCEControls_GetLocalPlayerState(state);
        uint32_t validity = 0;
        if (readOk)
        {
            validity |= state.hasControlledUnit ? 1u : 0u;
            validity |= state.onFoot ? 2u : 0u;
            validity |= state.nativePreparesFirstPerson ? 4u : 0u;
            validity |= !state.nativeInputBlocked ? 8u : 0u;
            validity |= !state.nativeLookBlocked ? 16u : 0u;
            validity |= !state.nativePaused ? 32u : 0u;
            validity |= !state.nativeCinematicFlag ? 64u : 0u;
            validity |= state.firstPersonVisible ? 128u : 0u;
        }
        PublishCaptureProbeResult(title, beginSeq, readOk, state.unit, state.weapon, validity);
    }

    void ProbeCaptureWeaponH2(GameTitle title) noexcept
    {
        const uint64_t beginSeq =
            Telemetry_PublishWeaponEvent(static_cast<uint8_t>(WeaponOrderEventKind::CaptureProbeBegin),
                static_cast<uint8_t>(WeaponOrderEventStatus::NoObservation), static_cast<uint8_t>(title),
                TitleAdapter_GetGeneration(title), g_preparedFrame.serial, UINT32_MAX, UINT32_MAX, 0, 0);
        uint32_t unit = UINT32_MAX, weapon = UINT32_MAX, detail = 0;
        const bool readOk = Halo2DiagnosticReadPrimaryWeapon(unit, weapon, detail);
        PublishCaptureProbeResult(title, beginSeq, readOk, unit, weapon, detail);
    }

    // Emits CapturePreLatch plus the title's direct pre-latch weapon probe.
    // Must be called immediately before UpdateTwoHandLatch() and nowhere else.
    void ProbeCaptureWeaponPreLatch() noexcept
    {
        if (!Telemetry_WeaponEventsAccepting())
            return;
        const GameTitle title = TitleAdapter_GetActiveTitle();
        PublishWeaponOrderMarker(WeaponOrderEventKind::CapturePreLatch);
        switch (title)
        {
        case GameTitle::HaloCE:
            ProbeCaptureWeaponCE(title);
            break;
        case GameTitle::Halo2:
            ProbeCaptureWeaponH2(title);
            break;
        case GameTitle::Halo3:
        case GameTitle::Halo3ODST:
        case GameTitle::HaloReach:
        case GameTitle::Halo4:
            ProbeCaptureWeaponTitle(title);
            break;
        default:
            break;
        }
    }
    // -----------------------------------------------------------------------
    // Persistent support grip (`persistent_support_grip`, default on)
    //
    // One durable owner-bound relationship with a single writer: the captured-
    // frame input path below. Every durable transition (bind / invalidate /
    // release) happens here; title hooks only publish owner evidence and read
    // the relationship, and the existing UpdateTwoHandHold() still owns the
    // spatial admission and logical retention of the physical grip.
    //
    // Rules this wiring keeps:
    //   - lifecycle identity (title / generation / mode) terminates and arms;
    //   - a fresh acquisition may bind only evidence published strictly after
    //       the revision the acquisition intent recorded;
    //   - a weapon gesture that consumes a held Grip arms requiresRelease
    //       before the owner is cleared;
    //   - a toggle click survives the physical button-up until it is proven;
    //   - one publication slot per title with a real writer acquisition;
    //   - the handedness/role teardown runs through this writer.
    //   Switch policy (PG ON only):
    //     a proven owner replacement drops the relationship WITHOUT arming
    //     release-before-reacquire, so a held Grip re-acquires the new weapon
    //     through ordinary spatial admission (the evidence floor stays intact). The optional
    //     `two_hand_switch_inherit` rebinds a complete current-generation
    //     replacement owner in place instead, so the switched weapon is
    //     two-handed immediately with no re-orientation. Proven absence and
    //     every generation/lifecycle/role/gesture teardown keep arming.
    // -----------------------------------------------------------------------

    // One title's evidence slot. `busy` is the writer acquisition: only the
    // thread that wins it may move the odd/even sequence, so two producers can
    // never interleave a record and a reader can never accept a torn one. The
    // per-title slot is what stops a foreign or retiring title's publication
    // from overwriting the title that currently owns the frame.
    struct SupportGripEvidenceSlot
    {
        std::atomic<bool> busy{false};
        std::atomic<uint32_t> sequence{0};
        std::atomic<uint8_t> title{static_cast<uint8_t>(GameTitle::None)};
        std::atomic<uint32_t> generation{0};
        std::atomic<uint8_t> ownerTitle{static_cast<uint8_t>(GameTitle::None)};
        std::atomic<uint32_t> ownerGeneration{0};
        std::atomic<uint32_t> ownerUnit{0xFFFFFFFFu};
        std::atomic<uint32_t> ownerWeapon{0xFFFFFFFFu};
        std::atomic<uint8_t> evidence{static_cast<uint8_t>(support_grip::OwnerEvidence::Unknown)};
        std::atomic<uint64_t> revision{0};
    };
    constinit SupportGripEvidenceSlot g_supportGripEvidenceSlots[kTitleRuntimeSlotCount];

    // Writer side. A lost acquisition or a title without a slot drops the
    // publication instead of tearing a record; the next producer update
    // republishes. The body stays lock-free, allocation-free and log-free (CAS
    // writer acquisition plus relaxed stores) because it runs on title-side
    // publisher threads: the FP/prepare/interpolate observation seams (CE
    // prepare, H2 level-live poll, H3/ODST/Reach FP interpolate, H4
    // model-skinning). It must never be called from a D3D palette-compose or
    // other hot compose hook.
    bool PublishSupportGripOwnerEvidenceInternal(GameTitle title, uint32_t generation,
        const support_grip::OwnerTuple& owner, support_grip::OwnerEvidence evidence) noexcept
    {
        const size_t slot = TitleRuntimeSlotIndex(title);
        if (slot >= kTitleRuntimeSlotCount)
            return false;
        SupportGripEvidenceSlot& target = g_supportGripEvidenceSlots[slot];
        bool expected = false;
        if (!target.busy.compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_acquire))
            return false; // another producer owns this slot right now
        const uint32_t sequence = target.sequence.load(std::memory_order_relaxed) + 1u;
        target.sequence.store(sequence, std::memory_order_release);
        target.title.store(static_cast<uint8_t>(title), std::memory_order_relaxed);
        target.generation.store(generation, std::memory_order_relaxed);
        target.ownerTitle.store(static_cast<uint8_t>(owner.title), std::memory_order_relaxed);
        target.ownerGeneration.store(owner.generation, std::memory_order_relaxed);
        target.ownerUnit.store(owner.unit, std::memory_order_relaxed);
        target.ownerWeapon.store(owner.weapon, std::memory_order_relaxed);
        target.evidence.store(static_cast<uint8_t>(evidence), std::memory_order_relaxed);
        // Monotonic per slot: a strictly newer publication is exactly what a
        // pending acquisition intent is allowed to bind from.
        target.revision.fetch_add(1, std::memory_order_relaxed);
        target.sequence.store(sequence + 1u, std::memory_order_release);
        target.busy.store(false, std::memory_order_release);
        return true;
    }

    // Coherent reader for the durable writer. False means "no evidence this
    // writer may act on": a never-published slot, a torn read, or a record for
    // another title/generation. A present claim that does not identify THIS
    // title's complete owner is reported as Unknown, because it is not proof of
    // a different owner either (only a proven claim may bind or
    // release).
    bool ReadSupportGripOwnerEvidenceInternal(GameTitle title, uint32_t expectedGeneration,
        support_grip::OwnerTuple& owner, support_grip::OwnerEvidence& evidence, uint64_t& revision) noexcept
    {
        owner = support_grip::OwnerTuple{};
        evidence = support_grip::OwnerEvidence::Unknown;
        revision = 0;
        const size_t slot = TitleRuntimeSlotIndex(title);
        if (slot >= kTitleRuntimeSlotCount || expectedGeneration == 0)
            return false;
        const SupportGripEvidenceSlot& source = g_supportGripEvidenceSlots[slot];
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const uint32_t before = source.sequence.load(std::memory_order_acquire);
            if (!before || (before & 1u))
                continue;
            const GameTitle recordedTitle = static_cast<GameTitle>(source.title.load(std::memory_order_relaxed));
            const uint32_t recordedGeneration = source.generation.load(std::memory_order_relaxed);
            support_grip::OwnerTuple candidate{};
            candidate.title = static_cast<GameTitle>(source.ownerTitle.load(std::memory_order_relaxed));
            candidate.generation = source.ownerGeneration.load(std::memory_order_relaxed);
            candidate.unit = source.ownerUnit.load(std::memory_order_relaxed);
            candidate.weapon = source.ownerWeapon.load(std::memory_order_relaxed);
            const support_grip::OwnerEvidence candidateEvidence =
                static_cast<support_grip::OwnerEvidence>(source.evidence.load(std::memory_order_relaxed));
            const uint64_t candidateRevision = source.revision.load(std::memory_order_relaxed);
            if (source.sequence.load(std::memory_order_acquire) != before)
                continue; // torn: retry once, then report unavailable
            if (recordedTitle != title || recordedGeneration != expectedGeneration)
                return false; // another lifecycle's record
            if (candidateEvidence == support_grip::OwnerEvidence::KnownPresent &&
                (!support_grip::OwnerComplete(candidate) || candidate.title != title))
                return true; // Unknown: not a proven owner of this title
            owner = candidate;
            evidence = candidateEvidence;
            revision = candidateRevision;
            return true;
        }
        return false;
    }

    // The durable relationship publication. Single writer by construction, so
    // a plain odd/even seqlock is sufficient here (unlike the per-title
    // evidence slots, which are written by title callbacks).
    struct SupportGripRelationshipState
    {
        std::atomic<uint32_t> sequence{0};
        std::atomic<uint8_t> title{static_cast<uint8_t>(GameTitle::None)};
        std::atomic<uint32_t> generation{0};
        std::atomic<uint32_t> unit{0xFFFFFFFFu};
        std::atomic<uint32_t> weapon{0xFFFFFFFFu};
        std::atomic<uint64_t> epoch{0};
        std::atomic<uint8_t> engaged{0};
    };
    constinit SupportGripRelationshipState g_supportGripRelationshipState;

    // Published BEFORE g_twoHandLatched moves, so the confirmed bind race
    // (latch already engaged while the publication still reads disengaged)
    // cannot happen: in that window a reader sees an engaged publication with
    // the old latch, which every owner-qualified consumer refuses instead of
    // accepting a support-derived ray as an ordinary one-hand ray.
    void PublishSupportGripRelationship(const support_grip::DurableWriterState& writer) noexcept
    {
        auto& published = g_supportGripRelationshipState;
        const GameTitle title = writer.engaged ? writer.owner.title : GameTitle::None;
        const uint32_t generation = writer.engaged ? writer.owner.generation : 0;
        const uint32_t unit = writer.engaged ? writer.owner.unit : 0xFFFFFFFFu;
        const uint32_t weapon = writer.engaged ? writer.owner.weapon : 0xFFFFFFFFu;
        published.sequence.fetch_add(1, std::memory_order_acq_rel);
        published.title.store(static_cast<uint8_t>(title), std::memory_order_relaxed);
        published.generation.store(generation, std::memory_order_relaxed);
        published.unit.store(unit, std::memory_order_relaxed);
        published.weapon.store(weapon, std::memory_order_relaxed);
        published.epoch.store(writer.epoch, std::memory_order_relaxed);
        published.engaged.store(writer.engaged ? 1u : 0u, std::memory_order_relaxed);
        published.sequence.fetch_add(1, std::memory_order_release);
    }
} // namespace
namespace vr_core
{
    bool ReadSupportGripRelationshipInternal(SupportGripRelationshipSnapshot& snapshot) noexcept
    {
        snapshot = SupportGripRelationshipSnapshot{};
        const SupportGripRelationshipState& published = g_supportGripRelationshipState;
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            const uint32_t before = published.sequence.load(std::memory_order_acquire);
            if (!before || (before & 1u))
                continue;
            SupportGripRelationshipSnapshot candidate{};
            candidate.title = static_cast<GameTitle>(published.title.load(std::memory_order_relaxed));
            candidate.generation = published.generation.load(std::memory_order_relaxed);
            candidate.unit = published.unit.load(std::memory_order_relaxed);
            candidate.weapon = published.weapon.load(std::memory_order_relaxed);
            candidate.epoch = published.epoch.load(std::memory_order_relaxed);
            candidate.engaged = published.engaged.load(std::memory_order_relaxed) != 0;
            if (published.sequence.load(std::memory_order_acquire) != before)
                continue; // torn: retry once, then report unavailable
            snapshot = candidate;
            return true;
        }
        return false;
    }
} // namespace vr_core
namespace
{
    // The physical support-grip acquisition volume, shared by the original
    // latch path and the persistent-grip writer. Grab-zone side nudge: the
    // visible barrel can sit beside the raw aim ray (headset report: the AR's
    // barrel was right of the zone), so the zone axis shifts along the
    // controller's +X by the F1-tuned amount. Palm depth moves where a support
    // grip counts: the grab zone meets the visible palm, not the wrist bone the
    // hand target anchors. Aim rays, shots, and contact/melee geometry stay on
    // the raw point.
    //
    // This is the BASE (PG-off / unwired-title) helper and its geometry is the
    // original rule verbatim, including the palm-depth shift along the SUPPORT
    // controller's own forward vector. The persistent-grip writer does not call
    // it: `PersistentSupportGrabZoneHit` below carries the original acquisition
    // rule, so a support-wrist rotation cannot orbit the
    // acquisition sample across the lateral boundary while the feature is on.
    // Grab-zone thresholds for the held weapon: its weapon_profile zone (the
    // original constants for any weapon without one), widened by the F1 extra
    // grab radius. Shared by both acquisition helpers below.
    support_grab::ZoneLimits CurrentTwoHandGrabLimits() noexcept
    {
        const GameTitle title = TitleAdapter_GetActiveTitle();
        const uint32_t generation = TitleAdapter_GetGeneration(title);
        const uint64_t identity = VR_GetWeaponModelIdentity(
            title, generation, g_contactSpaceEpoch.load(std::memory_order_acquire), GetTickCount64());
        // F1 Weapon tuning: a tuned zone (saved per weapon) replaces the
        // profile zone while all three values are set; not cached, it changes
        // live under the sliders.
        if (weapon_profile::GrabOverrideActive(
                g_config.two_hand_grab_centre_m, g_config.two_hand_grab_half_length_m, g_config.two_hand_grab_radius_m))
        {
            const weapon_profile::GrabZone zone = weapon_profile::GrabZoneOverride(
                weapon_profile::GrabZoneFor(title, identity), g_config.two_hand_grab_centre_m,
                g_config.two_hand_grab_half_length_m, g_config.two_hand_grab_radius_m);
            return support_grab::WidenedZone(support_grab::ZoneLimits{zone.alongMinM, zone.alongMaxM, zone.lateralMaxM},
                g_config.two_hand_grab_extra_m);
        }
        // Input thread only (both acquisition helpers run from the latch).
        static support_grab::ZoneLimitsCache cache;
        return cache.Get(static_cast<int>(title), identity, g_config.two_hand_grab_extra_m,
            [&]
            {
                const weapon_profile::GrabZone zone = weapon_profile::GrabZoneFor(title, identity);
                return support_grab::ZoneLimits{zone.alongMinM, zone.alongMaxM, zone.lateralMaxM};
            });
    }

    bool TwoHandGrabZoneHit(const XrPosef& rpose, const XrPosef& lpose,
        support_grab::ZoneLimits limits = CurrentTwoHandGrabLimits()) noexcept
    {
        const XrVector3f rfwd = Rotate(rpose.orientation, {0, 0, -1});
        const float zr = std::clamp(g_config.two_hand_zone_right_m, -0.10f, 0.10f);
        const XrVector3f rright = Rotate(rpose.orientation, {1, 0, 0});
        const XrVector3f origin{
            rpose.position.x + rright.x * zr, rpose.position.y + rright.y * zr, rpose.position.z + rright.z * zr};
        auto inZoneAt = [&](const XrVector3f& p) -> bool
        {
            const XrVector3f v{p.x - origin.x, p.y - origin.y, p.z - origin.z};
            const float along = v.x * rfwd.x + v.y * rfwd.y + v.z * rfwd.z;
            const XrVector3f perp{v.x - along * rfwd.x, v.y - along * rfwd.y, v.z - along * rfwd.z};
            const float lateral = sqrtf(perp.x * perp.x + perp.y * perp.y + perp.z * perp.z);
            return along > limits.alongMinM && along < limits.alongMaxM && lateral < limits.lateralMaxM;
        };
        const XrVector3f supportForward = Rotate(lpose.orientation, {0, 0, -1});
        const virtual_stock::Point3 grabPoint = virtual_stock::SupportGrabPoint(
            virtual_stock::Point3{lpose.position.x, lpose.position.y, lpose.position.z},
            virtual_stock::Point3{supportForward.x, supportForward.y, supportForward.z}, g_config.left_grip_forward_m);
        const XrVector3f grabSample{grabPoint.x, grabPoint.y, grabPoint.z};
        return inZoneAt(grabSample);
    }

    // PG ON acquisition (the original acquisition rule), used only by the durable writer
    // below, which runs only while VR_SupportGripWiredForTitle() is true. The
    // palm-depth shift is expressed along the stable primary acquisition axis
    // instead of the support controller's own forward vector, so the support
    // wrist orientation is deliberately not an input: rotating the wrist alone
    // can no longer orbit the acquisition sample across the lateral boundary,
    // while moving the hand still moves it naturally. Thresholds, the zone-right
    // nudge and its clamp, and the fail-closed non-finite behaviour are the
    // shared support_grab::EvaluateSupportGrabZone contract. The base helper
    // above keeps the original geometry verbatim for PG off / unwired titles.
    bool PersistentSupportGrabZoneHit(const XrPosef& rpose, const XrPosef& lpose,
        support_grab::ZoneLimits limits = CurrentTwoHandGrabLimits()) noexcept
    {
        const XrVector3f rfwd = Rotate(rpose.orientation, {0, 0, -1});
        const XrVector3f rright = Rotate(rpose.orientation, {1, 0, 0});
        const support_grab::SupportGrabZone zone = support_grab::EvaluateSupportGrabZone(
            virtual_stock::Point3{rpose.position.x, rpose.position.y, rpose.position.z},
            virtual_stock::Point3{rfwd.x, rfwd.y, rfwd.z}, virtual_stock::Point3{rright.x, rright.y, rright.z},
            g_config.two_hand_zone_right_m, virtual_stock::Point3{lpose.position.x, lpose.position.y, lpose.position.z},
            g_config.left_grip_forward_m, limits);
        return zone.inZone;
    }

    // The durable writer's own state. Single writer: UpdatePersistentSupportGrip
    // below, called once per captured frame from the input path. File scope (not
    // function statics) so a feature disable can retire it immediately instead
    // of leaving a relationship that a later re-enable would revive.
    struct PersistentSupportGripState
    {
        support_grip::DurableWriterState writer;
        support_grip::PendingAcquisitionIntent pending;
        bool published = false; // a coherent snapshot exists
        // The published (engaged, epoch, identity) marker: the snapshot moves
        // only on a real transition, never per frame.
        bool markerValid = false;
        bool markerEngaged = false;
        uint64_t markerEpoch = 0;
        GameTitle markerTitle = GameTitle::None;
        uint32_t markerGeneration = 0;
        bool blockLogged = false;
        bool dropLogged = false;
        bool live = false; // the feature owned the previous frame
        // 2026-09-28 headset follow-up, default weapon-switch re-grip UX. Set
        // by the no-inherit proven-replacement drop only
        // (support_grip::DefaultSwitchReplacementDrop): the relationship ended
        // without arming, and through a weapon swap the support hand is usually
        // still inside the NEW weapon's grab zone, so the level-triggered
        // hold-mode acquisition edge would re-bind the replacement on the next
        // frame and the switched weapon would arrive pre-held. While set, that
        // edge may fire only after the zone has been observed FALSE at least
        // once - the grab point leaves the zone and comes back, a fresh
        // crossing with no Grip release/press cycle
        // (support_grip::StepSwitchZoneReentry). Cleared by that observation,
        // by an explicit release, by a role/gesture/lifecycle teardown, by a
        // feature retire, and on the inherit path, which never sets it. Toggle
        // mode deliberately does not consult it: a fresh press is already a
        // deliberate acquisition.
        bool awaitZoneReentry = false;
    };
    constinit PersistentSupportGripState g_persistentSupportGrip;

    // The feature was switched off (or is off): retire the durable state so
    // the ordinary latch path below is the only owner. The physical release
    // edge and the latch itself stay with the base path, exactly like PG never
    // having been on.
    void RetirePersistentSupportGripState() noexcept
    {
        if (!g_persistentSupportGrip.live)
            return;
        g_persistentSupportGrip.live = false;
        // The default-switch crossing requirement never outlives the feature: a
        // later re-enable must start from ordinary admission.
        g_persistentSupportGrip.awaitZoneReentry = false;
        if (g_persistentSupportGrip.writer.engaged)
        {
            (void)support_grip::DurableWriterRelease(g_persistentSupportGrip.writer);
            support_diagnostics::Record(support_diagnostics::Event::Disabled);
        }
        g_persistentSupportGrip.pending = support_grip::PendingAcquisitionIntent{};
        g_persistentSupportGrip.markerValid = false;
        if (g_persistentSupportGrip.published)
            PublishSupportGripRelationship(g_persistentSupportGrip.writer);
    }

    // One update of the durable relationship. Called from the captured-frame
    // input path only, after the physical Grip edge and the existing admission
    // were observed. `admission` keeps owning the physical release edge; the
    // writer owns every decision that clears a bound owner.
    void UpdatePersistentSupportGrip(SupportGripAdmission& admission, bool rightValid, const XrPosef& rpose,
        bool leftValid, const XrPosef& lpose, bool gripHeld, bool rising, bool grabAllowed, bool rolesChanged,
        bool weaponGesture, bool freshPress)
    {
        support_grip::DurableWriterState& writer = g_persistentSupportGrip.writer;
        support_grip::PendingAcquisitionIntent& pending = g_persistentSupportGrip.pending;
        g_persistentSupportGrip.live = true;

        const GameTitle activeTitle = TitleAdapter_GetActiveTitle();
        const uint32_t activeGeneration = TitleAdapter_GetGeneration(activeTitle);
        const RuntimeMode mode = TitleAdapter_GetRuntimeMode();
        const bool lifecycleValid = support_grip::DurableLifecycleIdentifiesOwner(activeTitle, activeGeneration, mode);
        const bool identityLost = writer.engaged && support_grip::DurableRelationshipIdentityLost(
                                                        activeTitle, activeGeneration, mode, writer.owner);

        // Owner evidence for exactly this title lifecycle. Any incoherence
        // reads as Unknown: an unreadable or foreign record can never bind and
        // can never be used as proof of an owner break.
        support_grip::OwnerTuple evidenceOwner{};
        support_grip::OwnerEvidence evidence = support_grip::OwnerEvidence::Unknown;
        uint64_t evidenceRevision = 0;
        const bool evidenceCoherent = ReadSupportGripOwnerEvidenceInternal(
            activeTitle, activeGeneration, evidenceOwner, evidence, evidenceRevision);

        // A coherent disengaged snapshot exists from the first feature-on
        // update, so a consumer can never mistake "never published" for the
        // permanent state of the relationship.
        if (!g_persistentSupportGrip.published)
        {
            g_persistentSupportGrip.published = true;
            PublishSupportGripRelationship(writer);
        }

        const bool trackingValid = rightValid && leftValid;
        const bool configured = g_config.two_handed_aim;
        const bool toggleMode = g_config.two_hand_toggle;
        // User option: a proven switch inherits the two-hand hold immediately
        // (see support_grip::DurableWriterStep). PG-ON only by construction:
        // this function runs only behind the wired-title gate.
        const bool switchInherit = g_config.two_hand_switch_inherit;
        // Explicit release needs no poses: it is pure physical edge state.
        const bool explicitRelease = writer.engaged && (toggleMode ? rising : !gripHeld);
        // Teardowns that must run even through a pose outage, and that must arm
        // release-before-reacquire BEFORE the writer clears the owner
        // (title/generation/mode, interaction gesture, roles).
        const bool forcedTeardown = rolesChanged || weaponGesture || identityLost;
        if (writer.engaged && forcedTeardown)
            admission.Arm();
        // A role/gesture/lifecycle teardown (or a lifecycle replacement) is a
        // deliberate discontinuity: the default-switch crossing requirement
        // never survives it.
        if (forcedTeardown)
            g_persistentSupportGrip.awaitZoneReentry = false;
        writer.requiresRelease = admission.requiresRelease;

        if (!trackingValid && !forcedTeardown && !explicitRelease)
        {
            // A pose outage suspends the relationship
            // instead of erasing it. Nothing binds and nothing is abandoned, so
            // there is no window in which a recovered held Grip could silently
            // re-bind whatever owner the engine reached while the poses were
            // gone.
            ClearTwoHandActive();
            g_twoHandLatched.store(writer.engaged, std::memory_order_release);
            return;
        }

        const bool engagedBefore = writer.engaged;
        const support_grip::OwnerTuple previousOwner = writer.owner;
        support_grip::DurableWriterAction action = support_grip::DurableWriterAction::None;

        if (explicitRelease)
        {
            // Ordinary explicit release (hold-mode Grip release or
            // the toggle-mode off press). No arming and no owner-break
            // invalidation: the player released the relationship.
            pending = support_grip::PendingAcquisitionIntent{};
            // The player's own release is as deliberate as a fresh crossing:
            // the default-switch crossing requirement ends with it.
            g_persistentSupportGrip.awaitZoneReentry = false;
            action = support_grip::DurableWriterRelease(writer);
        }
        else
        {
            // PG ON acquisition geometry: the original acquisition predicate, not the
            // base helper. Both this call site and the base `TwoHandGrabZoneHit`
            // are behind the applicability gate (this function only runs while
            // VR_SupportGripWiredForTitle() is true), so PG off / unwired titles
            // keep the original geometry verbatim.
            const bool inZone = trackingValid && PersistentSupportGrabZoneHit(rpose, lpose);
            // 2026-09-28 headset follow-up: one update of the default-switch
            // crossing requirement (see the state member above and
            // support_grip::StepSwitchZoneReentry). A FALSE zone reading is the
            // fresh crossing the requirement waits for, and the hold-mode
            // acquisition edge may fire only with the requirement already
            // clear, so a still-held Grip that never left the replacement's
            // grab zone cannot re-bind it. Toggle mode keeps its own rising
            // edge: a fresh press is already deliberate.
            const support_grip::SwitchZoneReentryStep zoneReentry =
                support_grip::StepSwitchZoneReentry(g_persistentSupportGrip.awaitZoneReentry, gripHeld, inZone);
            g_persistentSupportGrip.awaitZoneReentry = zoneReentry.awaitZoneReentry;
            // Hold mode also needs a fresh press (two_hand_fresh_press): a
            // hand already squeezing does not latch by passing through.
            const bool acquisitionEdge =
                toggleMode ? (rising && inZone && !writer.engaged) : zoneReentry.holdAcquisitionEdge && freshPress;
            // An acquisition intent may begin only from a provable
            // evidence revision, because its floor is what stops a cached
            // previous-owner publication from creating the relation. Dropping
            // the click (throttled, logged) is the fail-closed choice.
            const bool intentEdge = acquisitionEdge && evidenceCoherent;
            if (acquisitionEdge && !evidenceCoherent)
            {
                // Once per acquisition episode, not per frame: the same click
                // stays unserviceable until the producer publishes.
                if (!g_persistentSupportGrip.dropLogged)
                {
                    g_persistentSupportGrip.dropLogged = true;
                    support_diagnostics::Record(support_diagnostics::Event::AcquisitionEvidenceMissing);
                }
            }
            else if (!acquisitionEdge)
                g_persistentSupportGrip.dropLogged = false;
            support_grip::PendingAcquisitionInputs inputs{};
            inputs.engaged = writer.engaged;
            inputs.acquisitionEdge = intentEdge;
            // A second toggle press cancels a pending intent; an intent
            // that already exists is never silently absorbed by the next click.
            inputs.cancelRequested = toggleMode && rising && (writer.engaged || pending.pending);
            // Cancel policy: a title/generation/lifecycle/role/secondary/
            // gesture invalidation ends the intent. A pose outage never reaches
            // this code, so it neither cancels nor advances the intent.
            inputs.lifecycleValid = lifecycleValid && configured && grabAllowed && !weaponGesture;
            inputs.holdMode = !toggleMode;
            inputs.gripHeld = gripHeld;
            inputs.title = activeTitle;
            inputs.generation = activeGeneration;
            inputs.publicationRevision = evidenceCoherent ? evidenceRevision : 0;
            (void)support_grip::PendingAcquisitionStep(pending, inputs);
            // The pending intent IS the acquisition request: spatial admission
            // already succeeded for it, and the fresh-owner proof gates it, so
            // a bind can never come from stale prior-owner evidence.
            const bool request = trackingValid && support_grip::PendingAcquisitionRequest(pending, activeTitle,
                                                      activeGeneration, evidence, evidenceRevision);
            const bool hardGate =
                configured && grabAllowed && !weaponGesture && !rolesChanged && lifecycleValid && !identityLost;
            action = support_grip::DurableWriterStep(
                writer, hardGate && trackingValid, request, evidence, evidenceOwner, activeGeneration, switchInherit);
        }

        // 2026-09-28 headset follow-up: the no-inherit stage-2 owner break is
        // the transition that must not pre-hold the replacement weapon, so it
        // arms the crossing requirement for the very next update. It is the
        // only transition that reaches this site: the inherit option rebinds in
        // place (Bind), a proven absence and a generation replacement arm
        // release-before-reacquire inside the writer, and any role/gesture/
        // lifecycle teardown answers at the lifecycle gate first.
        if (support_grip::DefaultSwitchReplacementDrop(engagedBefore, action,
                evidence == support_grip::OwnerEvidence::KnownPresent &&
                    !support_grip::SameOwner(evidenceOwner, previousOwner),
                identityLost, switchInherit))
            g_persistentSupportGrip.awaitZoneReentry = true;

        // One arming state, two views: the writer's owner-break and generation
        // decisions are mirrored into the admission the physical release edge
        // clears.
        if (writer.requiresRelease && !admission.requiresRelease)
            admission.Arm();

        // An owner break and a lifecycle (generation) replacement must
        // not leave Lab damping or input-filter history from the previous
        // owner. For VS-OFF product continuity, a durable owner break that
        // releases an already-presented latch is itself the latch-release edge:
        // preserve that one release bridge to raw one-hand aim. Other owner
        // breaks keep the prior invalidation behavior. The prepared seam still
        // independently invalidates title/generation changes. An inherited
        // switch is not a break at all and leaves both layers alone.
        if (action == support_grip::DurableWriterAction::Invalidated || identityLost)
        {
            const bool productReleasePending =
                !CurrentResolvedVirtualStockAimSettings().settings.virtualStockEnabled &&
                TwoHandTransitionContinuityEnabled() && !writer.engaged && !identityLost &&
                ((g_aimContinuityLayer.transition.initialized && g_aimContinuityLayer.transition.previousLatched) ||
                    (g_aimContinuityLayer.transition.active &&
                        g_aimContinuityLayer.transition.phase == virtual_stock::AimContinuityPhase::Release));
            if (productReleasePending)
                InvalidateTwoHandInputSmoothingLayer();
            else
                InvalidateAimContinuityLayer();
            InvalidateTwoHandLabTemporal();
        }

        if (engagedBefore && !writer.engaged)
        {
            // Diagnostics: an explicit player release (hold-mode
            // Grip release or the toggle-mode off press) is reported as
            // itself FIRST. In the old ladder "explicit release" was only the
            // unreachable-by-another-rung default, so a plain release that
            // happened while a same-owner KnownPresent record was published
            // printed "proven owner replacement" (confirmed in the installed
            // CE logs: 10/10 releases). The evidence rungs below now describe
            // only transitions the writer itself decided, and the detail
            // lines stay scoped to those (transition-only logging is
            // unchanged).
            auto reason = support_diagnostics::Event::Released;
            if (explicitRelease)
                reason = support_diagnostics::Event::ReleasedExplicit;
            else if (rolesChanged)
                reason = support_diagnostics::Event::ReleasedHandedness;
            else if (weaponGesture)
                reason = support_diagnostics::Event::ReleasedGesture;
            else if (identityLost)
                reason = support_diagnostics::Event::ReleasedLifecycle;
            else if (!configured)
                reason = support_diagnostics::Event::ReleasedDisabled;
            else if (!grabAllowed)
                reason = support_diagnostics::Event::ReleasedAdmission;
            else if (evidence == support_grip::OwnerEvidence::KnownAbsent)
                reason = support_diagnostics::Event::WeaponAbsent;
            else if (evidence == support_grip::OwnerEvidence::KnownPresent)
                reason = support_diagnostics::Event::Replacement;
            support_diagnostics::Record(reason);
            // The evidence detail belongs to the writer-decided transitions
            // (owner break / lifecycle replacement), not to a player release
            // that merely happened while some record was published.
            if (!explicitRelease && evidence == support_grip::OwnerEvidence::KnownPresent &&
                !support_grip::SameOwner(evidenceOwner, previousOwner))
                support_diagnostics::Record(support_diagnostics::Event::Replacement);
            else if (!explicitRelease && evidence == support_grip::OwnerEvidence::KnownAbsent)
                support_diagnostics::Record(support_diagnostics::Event::WeaponAbsent);
        }
        if (action == support_grip::DurableWriterAction::Bind)
        {
            if (engagedBefore && !support_grip::SameOwner(previousOwner, writer.owner))
            {
                // two_hand_switch_inherit: a proven replacement was rebound in
                // place (epoch advanced, still engaged, no arming and no owner-break
                // reset), so the switched weapon is two-handed immediately.
                // The crossing requirement belongs to the default drop and this
                // option never arms it; clearing it here keeps the option's
                // semantics exactly as shipped.
                g_persistentSupportGrip.awaitZoneReentry = false;
                support_diagnostics::Record(support_diagnostics::Event::SwitchInherited);
            }
            else
            {
                support_diagnostics::Record(support_diagnostics::Event::Bound);
            }
        }
        if (!writer.engaged && !grabAllowed && (gripHeld || pending.pending) && admission.requiresRelease &&
            !g_persistentSupportGrip.blockLogged)
        {
            g_persistentSupportGrip.blockLogged = true;
            support_diagnostics::Record(support_diagnostics::Event::ReleaseRequired);
        }
        if (!admission.requiresRelease)
            g_persistentSupportGrip.blockLogged = false;

        const GameTitle publishedTitle = writer.engaged ? writer.owner.title : GameTitle::None;
        const uint32_t publishedGeneration = writer.engaged ? writer.owner.generation : 0;
        if (!g_persistentSupportGrip.markerValid || g_persistentSupportGrip.markerEngaged != writer.engaged ||
            g_persistentSupportGrip.markerEpoch != writer.epoch ||
            g_persistentSupportGrip.markerTitle != publishedTitle ||
            g_persistentSupportGrip.markerGeneration != publishedGeneration)
        {
            g_persistentSupportGrip.markerValid = true;
            g_persistentSupportGrip.markerEngaged = writer.engaged;
            g_persistentSupportGrip.markerEpoch = writer.epoch;
            g_persistentSupportGrip.markerTitle = publishedTitle;
            g_persistentSupportGrip.markerGeneration = publishedGeneration;
            PublishSupportGripRelationship(writer);
        }
        g_twoHandLatched.store(writer.engaged, std::memory_order_release);
        if (!writer.engaged)
            ClearTwoHandActive();
    }

    constinit support_grip::GripPress g_supportGripPress;      // input thread
    constinit support_grip::GripPressStep g_supportGripStep{}; // this frame's press, for the feedback tick
    void UpdateTwoHandLatch(bool rightValid, const XrPosef& rpose, bool leftValid, const XrPosef& lpose, float gripL,
        bool rolesChanged, bool weaponGesture = false)
    {
        // Track the physical grip edge even while tracking/handedness resets
        // invalidate the poses. A held grip is not a new grab on recovery.
        const support_grip::GripPressStep press = support_grip::StepGripPress(
            g_supportGripPress, gripL, GetTickCount64(), g_config.two_hand_grip_press, g_config.two_hand_grip_release);
        g_supportGripStep = press;
        const bool gripHeld = press.held;
        const bool rising = press.rising;
        const bool freshPress = !g_config.two_hand_fresh_press || press.fresh;
        static SupportGripAdmission admission;
        const bool grabAllowed = admission.Observe(gripHeld, SecondaryWeaponPresentationActive(), rolesChanged);
        // Transitional applicability gate: the durable writer only owns a
        // wired title. An unwired title (ODST/Reach/H4 until they are wired)
        // runs the EXACT base latch path below even with the config
        // on, and retires any dormant durable state so a later wired title
        // cannot inherit it.
        if (VR_SupportGripWiredForTitle(TitleAdapter_GetActiveTitle()))
        {
            // The durable writer replaces the latch decision.
            UpdatePersistentSupportGrip(admission, rightValid, rpose, leftValid, lpose, gripHeld, rising, grabAllowed,
                rolesChanged, weaponGesture, freshPress);
            return;
        }
        // The feature is off or this title is not wired yet: retire any durable
        // state a previous frame owned, so re-enabling it (or switching to a
        // wired title) cannot revive a relationship the player never
        // re-acquired. The base path below then owns the frame unchanged.
        RetirePersistentSupportGripState();
        if (!grabAllowed || !g_config.two_handed_aim || !rightValid || !leftValid || weaponGesture)
        {
            g_twoHandLatched.store(false);
            ClearTwoHandActive();
            return;
        }
        // Hold mode grabs only on a fresh press (two_hand_fresh_press); an
        // engaged hold or toggle mode is unaffected.
        const bool inZone =
            TwoHandGrabZoneHit(rpose, lpose) && (g_config.two_hand_toggle || g_twoHandLatched.load() || freshPress);

        if (g_config.two_hand_toggle)
        {
            if (rising)
            {
                if (g_twoHandLatched.load())
                    g_twoHandLatched.store(false); // toggle off
                else if (inZone)
                    g_twoHandLatched.store(true); // toggle on
            }
        }
        else // hold mode
        {
            g_twoHandLatched.store(UpdateTwoHandHold(g_twoHandLatched.load(), gripHeld, inZone));
        }
    }

    // Called right after UpdateTwoHandLatch (base or durable writer): a
    // support-hand haptic tick on grab and a softer one on a deliberate
    // let-go (grip released in hold mode, a fresh click in toggle mode).
    void UpdateTwoHandGrabFeedback(bool leftValid, float gripL)
    {
        static support_grab::GrabFeedback feedback;
        (void)gripL; // the press state the latch just stepped, same thresholds
        const bool held = g_supportGripStep.held;
        const bool rising = g_supportGripStep.rising;
        const bool deliberate = g_config.two_hand_toggle ? rising : !held;
        // Attach/release edges in the log.
        static bool wasLatched = false;
        const bool latchedNow = g_twoHandLatched.load(std::memory_order_acquire);
        if (latchedNow != wasLatched)
        {
            static uint64_t lastLogMs = 0;
            const uint64_t now = GetTickCount64();
            if (now - lastLogMs >= 250)
            {
                LOG("Action fired: support grip %s", latchedNow ? "attached" : "released");
                lastLogMs = now;
            }
            wasLatched = latchedNow;
        }
        const float tick = feedback.Update(
            g_twoHandLatched.load(std::memory_order_acquire), deliberate, g_config.two_hand_grab_haptic);
        if (tick > 0.0f && leftValid)
            VR_PulseContactHaptics(true, tick);
    }

    void ApplyControllerHaptics(bool primaryTrackingValid, bool supportTrackingValid)
    {
        static bool active[2]{false, false};
        static uint64_t lastApplyMs = 0;
        static RuntimeMode previousMode = RuntimeMode::Shell;
        static GameTitle previousTitle = GameTitle::None;
        static uint32_t previousGeneration = 0;
        const GameTitle title = TitleAdapter_GetActiveTitle();
        const uint32_t generation = TitleAdapter_GetGeneration(title);
        const uint64_t hapticNow = GetTickCount64();
        if (title != previousTitle || generation != previousGeneration)
        {
            g_weaponHaptics.Clear();
            g_weaponHapticAdmission.Invalidate();
            previousTitle = title;
            previousGeneration = generation;
        }
        const bool coupledSupport = primaryTrackingValid && supportTrackingValid && VR_IsTwoHandAiming();
        const RuntimeMode mode = TitleAdapter_GetRuntimeMode();
        const bool modeAllows =
            mode == RuntimeMode::Gameplay || mode == RuntimeMode::Vehicle || mode == RuntimeMode::Turret;
        const bool capabilityAllows = Game_HasTitleCapability(TitleCapability_Haptics);
        const bool tracked[2]{supportTrackingValid, primaryTrackingValid};
        const bool leftHanded = g_capturedLeftHanded.load(std::memory_order_acquire);
        const XrPath paths[2]{
            leftHanded ? g_rightHandPath : g_leftHandPath, leftHanded ? g_leftHandPath : g_rightHandPath};
        // A missing support controller must not silence the tracked weapon
        // hand. Stop only the lost hand immediately, before the reapply timer.
        for (int hand = 0; hand < 2; ++hand)
        {
            if (tracked[hand])
                continue;
            (void)g_weaponHaptics.Read(title, generation, hand, true, hapticNow);
            if (hand == 0)
                (void)g_weaponHaptics.Read(title, generation, 2, true, hapticNow);
            g_contactHaptics[hand].store(0.0f, std::memory_order_release);
            if (active[hand])
            {
                if (g_session != XR_NULL_HANDLE && g_hapticAction != XR_NULL_HANDLE)
                {
                    XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
                    info.action = g_hapticAction;
                    info.subactionPath = paths[hand];
                    xrStopHapticFeedback(g_session, &info);
                }
                active[hand] = false;
            }
        }
        if (!capabilityAllows)
        {
            // Stop is not enough: the requested amplitude is persistent. Drop
            // it while ownership/arming is absent so it cannot be replayed
            // after a title transition without a fresh XInput request. The
            // accumulated peak is dropped for the same reason.
            g_requestedHaptics.store(0.0f, std::memory_order_release);
            g_peakHaptics.store(0.0f, std::memory_order_release);
            g_contactHaptics[0].store(0.0f, std::memory_order_release);
            g_contactHaptics[1].store(0.0f, std::memory_order_release);
        }
        const float intensity =
            std::isfinite(g_config.haptic_intensity) ? std::clamp(g_config.haptic_intensity, 0.0f, 1.0f) : 0.0f;
        const bool feedbackUnavailable = (!tracked[0] && !tracked[1]) || !modeAllows || !capabilityAllows ||
                                         Menu_IsOpen() || g_sessionState != XR_SESSION_STATE_FOCUSED ||
                                         g_session == XR_NULL_HANDLE || g_hapticAction == XR_NULL_HANDLE ||
                                         intensity <= 0.0f;
        for (int hand = 0; hand < 2; ++hand)
            g_weaponHapticAdmission.Publish(hand, tracked[hand] && !feedbackUnavailable, hapticNow);
        const uint64_t handTokens[2]{
            g_weaponHapticAdmission.Token(0, hapticNow), g_weaponHapticAdmission.Token(1, hapticNow)};
        // Peak-hold: peek (without consuming) the max amplitude requested since
        // the last applied frame so a short gunfire pulse (SetState(high) then
        // SetState(0) between two frame samples) cannot be aliased to zero.
        const float latest =
            capabilityAllows ? std::clamp(g_requestedHaptics.load(std::memory_order_acquire), 0.0f, 1.0f) : 0.0f;
        const float peekPeak = capabilityAllows ? g_peakHaptics.load(std::memory_order_acquire) : 0.0f;
        const float contactPeek[2]{
            g_contactHaptics[0].load(std::memory_order_acquire), g_contactHaptics[1].load(std::memory_order_acquire)};
        const float gamePeek = SampleHapticPeak(peekPeak, latest).apply;
        if (!coupledSupport)
            (void)g_weaponHaptics.Read(title, generation, 2, true, hapticNow);
        const float weaponPeek[2]{
            MergeHapticAmplitude(g_weaponHaptics.Read(title, generation, 0, false, hapticNow, handTokens[0]),
                coupledSupport
                    ? g_weaponHaptics.Read(title, generation, 2, false, hapticNow, handTokens[1], handTokens[0])
                    : 0.0f),
            g_weaponHaptics.Read(title, generation, 1, false, hapticNow, handTokens[1])};
        const float peekAmplitude[2]{
            tracked[0] ? MergeHapticAmplitude(MergeHapticAmplitude(gamePeek, contactPeek[0]), weaponPeek[0]) * intensity
                       : 0.0f,
            tracked[1] ? MergeHapticAmplitude(MergeHapticAmplitude(gamePeek, contactPeek[1]), weaponPeek[1]) * intensity
                       : 0.0f};
        const bool anyAmplitude = peekAmplitude[0] > 0.0f || peekAmplitude[1] > 0.0f;
        const bool mustStop = !anyAmplitude || feedbackUnavailable;
        if (mustStop)
        {
            // Collision pulses are momentary contact evidence. Never replay a
            // pulse that arrived while tracking, focus, or gameplay was absent.
            // Do not clear on the ordinary zero-amplitude path: a collision
            // worker may race the peek above and its newly raised peak must be
            // preserved for the next VR frame.
            if (feedbackUnavailable)
            {
                g_weaponHaptics.Clear();
                g_requestedHaptics.store(0.0f, std::memory_order_release);
                g_peakHaptics.store(0.0f, std::memory_order_release);
                g_contactHaptics[0].store(0.0f, std::memory_order_release);
                g_contactHaptics[1].store(0.0f, std::memory_order_release);
            }
            if (active[0] || active[1] || mode != previousMode)
                StopControllerHaptics();
            active[0] = false;
            active[1] = false;
            previousMode = mode;
            return;
        }
        previousMode = mode;

        for (int hand = 0; hand < 2; ++hand)
            if (active[hand] && peekAmplitude[hand] <= 0.0f)
            {
                XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
                info.action = g_hapticAction;
                info.subactionPath = paths[hand];
                xrStopHapticFeedback(g_session, &info);
                active[hand] = false;
            }

        const uint64_t now = GetTickCount64();
        if ((active[0] || active[1]) && now - lastApplyMs < 40)
            return; // keep accumulating the peak; apply on the next unthrottled frame
        // Applying now: consume the peak and carry the latest sustained value
        // forward, so a one-shot pulse fires exactly once and a held rumble
        // persists across the 40 ms re-apply throttle.
        const float appliedPeak = g_peakHaptics.exchange(latest, std::memory_order_acq_rel);
        const float gameAmplitude = SampleHapticPeak(appliedPeak, latest).apply;
        const float contactAmplitude[2]{g_contactHaptics[0].exchange(0.0f, std::memory_order_acq_rel),
            g_contactHaptics[1].exchange(0.0f, std::memory_order_acq_rel)};
        const float supportWeaponAmplitude =
            g_weaponHaptics.Read(title, generation, 2, true, hapticNow, handTokens[1], handTokens[0]);
        const float weaponAmplitude[2]{
            MergeHapticAmplitude(g_weaponHaptics.Read(title, generation, 0, true, hapticNow, handTokens[0]),
                coupledSupport ? supportWeaponAmplitude : 0.0f),
            g_weaponHaptics.Read(title, generation, 1, true, hapticNow, handTokens[1])};
        const float amplitude[2]{
            tracked[0]
                ? MergeHapticAmplitude(MergeHapticAmplitude(gameAmplitude, contactAmplitude[0]), weaponAmplitude[0]) *
                      intensity
                : 0.0f,
            tracked[1]
                ? MergeHapticAmplitude(MergeHapticAmplitude(gameAmplitude, contactAmplitude[1]), weaponAmplitude[1]) *
                      intensity
                : 0.0f};
        for (int hand = 0; hand < 2; ++hand)
        {
            XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
            info.action = g_hapticAction;
            info.subactionPath = paths[hand];
            if (amplitude[hand] > 0.0f)
            {
                XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
                vibration.amplitude = amplitude[hand];
                vibration.duration = 50000000;
                vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
                xrApplyHapticFeedback(g_session, &info, reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
                active[hand] = true;
            }
            else if (active[hand])
            {
                xrStopHapticFeedback(g_session, &info);
                active[hand] = false;
            }
        }
        lastApplyMs = now;
    }

    bool ReadLeftThumbrestTouched()
    {
        if (g_actLeftThumbrest == XR_NULL_HANDLE)
            return false;
        XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
        info.action = g_actLeftThumbrest;
        XrActionStateBoolean touch{XR_TYPE_ACTION_STATE_BOOLEAN};
        return XR_SUCCEEDED(xrGetActionStateBoolean(g_session, &info, &touch)) && touch.isActive &&
               touch.currentState == XR_TRUE;
    }

    void InvalidateWeaponInteractionSample()
    {
        exclusive_input::active.store(false, std::memory_order_release);
        g_weaponAccessory = {};
        g_weaponInteraction.Cancel();
        g_weaponGestureReadyMs.store(0, std::memory_order_release);
        EnterCriticalSection(&g_headCs);
        g_padState.weaponSampleMs = 0;
        g_padState.weaponButtons = 0;
        g_padState.weaponConsumePrimary = g_padState.weaponConsumeSupport = false;
        LeaveCriticalSection(&g_headCs);
    }

    // Position-only grip sample for one hand. Admission is purely the OpenXR
    // action/space availability plus this frame's tracking validity: there is
    // deliberately NO feature/config gate. The grip positions are the
    // positional pivots of the free two-hand production geometry (Grip -> Grip)
    // and of the optional support endpoint, so they must exist regardless of
    // any Virtual Stock or "Reduce Support-Hand Rotation" selection. Failure
    // closes the sample for that hand only; the callers keep their previous
    // fail-open behaviour.
    bool TryLocateSupportGripPosition(XrPath handPath, XrSpace gripSpace, XrTime time, XrVector3f& outPosition)
    {
        if (g_supportGripPoseAction == XR_NULL_HANDLE || gripSpace == XR_NULL_HANDLE)
            return false;
        XrActionStateGetInfo gripGet{XR_TYPE_ACTION_STATE_GET_INFO};
        gripGet.action = g_supportGripPoseAction;
        gripGet.subactionPath = handPath;
        XrActionStatePose gripState{XR_TYPE_ACTION_STATE_POSE};
        XrSpaceLocation gripLocation{XR_TYPE_SPACE_LOCATION};
        if (XR_FAILED(xrGetActionStatePose(g_session, &gripGet, &gripState)) || !gripState.isActive ||
            XR_FAILED(xrLocateSpace(gripSpace, g_localSpace, time, &gripLocation)))
            return false;
        if ((gripLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == 0)
            return false;
        const XrVector3f position = gripLocation.pose.position;
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
            return false;
        outPosition = position;
        return true;
    }
} // namespace
namespace vr_core
{
    bool CaptureRightControllerPose(XrTime time)
    {
        g_controllerFingerSample = {};
        g_primaryGripPoseValid = false;
        if (g_gameplayActions == XR_NULL_HANDLE || g_rightAimAction == XR_NULL_HANDLE ||
            g_rightAimSpace == XR_NULL_HANDLE)
        {
            g_thumbrestDpadSampleMs.store(0, std::memory_order_release);
            g_supportGripPoseFresh.store(false, std::memory_order_release);
            InvalidateWeaponInteractionSample();
            return false;
        }
        XrActiveActionSet active{g_gameplayActions, XR_NULL_PATH};
        XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
        sync.countActiveActionSets = 1;
        sync.activeActionSets = &active;
        const XrResult syncResult = xrSyncActions(g_session, &sync);
        if (XR_FAILED(syncResult))
        {
            // Pad-feed diagnostic: a failed sync means no buttons reach anything.
            static uint64_t lastSyncFailLogMs = 0;
            const uint64_t syncFailNow = GetTickCount64();
            if (syncFailNow - lastSyncFailLogMs >= 1000)
            {
                lastSyncFailLogMs = syncFailNow;
                LOG("pad feed: xrSyncActions(gameplay set) FAILED %s; session state %d; no controller input this frame",
                    XrStr(syncResult), int(g_sessionState));
            }
            g_thumbrestDpadSampleMs.store(0, std::memory_order_release);
            g_supportGripPoseFresh.store(false, std::memory_order_release);
            InvalidateWeaponInteractionSample();
            return false;
        }
        XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};
        get.action = g_rightAimAction;
        get.subactionPath = g_rightHandPath;
        XrActionStatePose state{XR_TYPE_ACTION_STATE_POSE};
        bool valid = false;
        XrSpaceVelocity velocity{XR_TYPE_SPACE_VELOCITY};
        XrSpaceLocation location{XR_TYPE_SPACE_LOCATION, &velocity};
        if (XR_SUCCEEDED(xrGetActionStatePose(g_session, &get, &state)) && state.isActive &&
            XR_SUCCEEDED(xrLocateSpace(g_rightAimSpace, g_localSpace, time, &location)))
        {
            constexpr XrSpaceLocationFlags required =
                XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
            valid = (location.locationFlags & required) == required && NormalizeTrackedPose(location.pose);
        }
        // Left hand: position only matters (D-pad gesture), same locate path.
        bool leftValid = false;
        XrSpaceVelocity leftVelocity{XR_TYPE_SPACE_VELOCITY};
        XrSpaceLocation leftLocation{XR_TYPE_SPACE_LOCATION, &leftVelocity};
        if (g_leftAimAction != XR_NULL_HANDLE && g_leftAimSpace != XR_NULL_HANDLE)
        {
            get.action = g_leftAimAction;
            get.subactionPath = g_leftHandPath;
            XrActionStatePose leftState{XR_TYPE_ACTION_STATE_POSE};
            if (XR_SUCCEEDED(xrGetActionStatePose(g_session, &get, &leftState)) && leftState.isActive &&
                XR_SUCCEEDED(xrLocateSpace(g_leftAimSpace, g_localSpace, time, &leftLocation)))
            {
                constexpr XrSpaceLocationFlags required =
                    XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
                leftValid =
                    (leftLocation.locationFlags & required) == required && NormalizeTrackedPose(leftLocation.pose);
            }
        }

        // Both hand grip positions are sampled on every capture, independent of
        // any Virtual Stock or "Reduce Support-Hand Rotation" selection: the
        // free two-hand production geometry consumes the primary -> support
        // Grip pair directly, and the support endpoint selector consumes the
        // same sample. These reuse the existing OpenXR grip action/spaces and
        // the frame's single xrSyncActions sample - no extra runtime sampling.
        XrVector3f physicalGripPosition[2]{};
        bool physicalGripValid[2]{};
        physicalGripValid[0] =
            TryLocateSupportGripPosition(g_leftHandPath, g_leftGripPoseSpace, time, physicalGripPosition[0]);
        physicalGripValid[1] =
            TryLocateSupportGripPosition(g_rightHandPath, g_rightGripPoseSpace, time, physicalGripPosition[1]);

        auto selectVelocity = [&](int hand, bool poseValid, const XrPosef& pose, const XrSpaceVelocity& native,
                                  XrVector3f& selected, bool& selectedValid, bool& usedPoseDerived)
        {
            ControllerPoseVelocityHistory& history = g_controllerPoseVelocityHistory[hand];
            float derived[3]{};
            const float current[3]{pose.position.x, pose.position.y, pose.position.z};
            const float previous[3]{history.position.x, history.position.y, history.position.z};
            const bool derivedValid =
                poseValid && history.valid && time > history.sampleTime &&
                Halo4DeriveControllerPoseVelocity(previous, current, time - history.sampleTime, derived);
            if (poseValid)
            {
                history.valid = true;
                history.sampleTime = time;
                history.position = pose.position;
            }
            else
            {
                history = {};
            }
            const bool nativeValid = poseValid && (native.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) != 0 &&
                                     std::isfinite(native.linearVelocity.x) && std::isfinite(native.linearVelocity.y) &&
                                     std::isfinite(native.linearVelocity.z);
            const float nativeValue[3]{native.linearVelocity.x, native.linearVelocity.y, native.linearVelocity.z};
            float chosen[3]{};
            selectedValid = Halo4SelectControllerLinearVelocity(
                nativeValid, nativeValue, derivedValid, derived, chosen, usedPoseDerived);
            if (selectedValid)
                selected = {chosen[0], chosen[1], chosen[2]};
        };
        XrVector3f selectedRightVelocity{}, selectedLeftVelocity{};
        bool selectedRightVelocityValid = false;
        bool selectedLeftVelocityValid = false;
        bool rightUsedPoseDerived = false, leftUsedPoseDerived = false;
        selectVelocity(
            1, valid, location.pose, velocity, selectedRightVelocity, selectedRightVelocityValid, rightUsedPoseDerived);
        selectVelocity(0, leftValid, leftLocation.pose, leftVelocity, selectedLeftVelocity, selectedLeftVelocityValid,
            leftUsedPoseDerived);

        const bool leftHanded = g_config.left_handed;
        const bool handChanged = leftHanded != g_capturedLeftHanded.load(std::memory_order_acquire);
        if (handChanged)
        {
            StopControllerHaptics();
            LOG("Weapon handedness: physical %s hand is primary; physical %s hand is support/secondary",
                leftHanded ? "left" : "right", leftHanded ? "right" : "left");
            g_contactSpaceEpoch.fetch_add(1, std::memory_order_acq_rel);
            // With the persistent grip wired for the active title, this
            // teardown must run through the canonical writer below (the same
            // edge reaches it as `rolesChanged`) so the durable relationship's
            // owner, epoch and publication move together instead of clearing
            // the latch out of band and leaving a stale owner behind. PG off
            // *and every unwired title* keep the original direct clear (the
            // applicability gate: base behaviour on unwired titles).
            if (!VR_SupportGripWiredForTitle(TitleAdapter_GetActiveTitle()))
                g_twoHandLatched.store(false, std::memory_order_release);
            g_contactHaptics[0].store(0.0f, std::memory_order_release);
            g_contactHaptics[1].store(0.0f, std::memory_order_release);
            // Handedness swaps the semantic primary/support roles, so a stored
            // transition anchor would be replayed against the wrong hand.
            InvalidateAimContinuityLayer();
            // A stored Lab temporal correction/seed belongs to the old
            // semantic roles for the same reason.
            InvalidateTwoHandLabTemporal();
        }
        EnterCriticalSection(&g_headCs);
        g_physicalControllerPose[0] = leftLocation.pose;
        g_physicalControllerPose[1] = location.pose;
        g_physicalControllerValid[0] = leftValid;
        g_physicalControllerValid[1] = valid;
        g_physicalControllerAtMs = GetTickCount64();
        if (leftHanded)
        {
            std::swap(location.pose, leftLocation.pose);
            std::swap(valid, leftValid);
            std::swap(selectedRightVelocity, selectedLeftVelocity);
            std::swap(selectedRightVelocityValid, selectedLeftVelocityValid);
            std::swap(physicalGripPosition[0], physicalGripPosition[1]);
            std::swap(physicalGripValid[0], physicalGripValid[1]);
        }
        g_capturedLeftHanded.store(leftHanded, std::memory_order_release);
        if (handChanged)
        {
            // Prevent a toggle from appearing as a swing between controllers.
            valid = leftValid = false;
            selectedRightVelocityValid = selectedLeftVelocityValid = false;
            physicalGripValid[0] = physicalGripValid[1] = false;
            g_meleeSpeedHistory[0] = {};
            g_meleeSpeedHistory[1] = {};
        }
        g_rightAimPoseValid = valid;
        g_stockControllerPoseTime = valid && leftValid ? time : 0;
        if (valid)
            g_rightAimPose = location.pose;
        g_rightAimLinearVelocityValid = selectedRightVelocityValid;
        if (g_rightAimLinearVelocityValid)
            g_rightAimLinearVelocity = selectedRightVelocity;
        g_rightAimLinearVelocityAtMs = g_rightAimLinearVelocityValid ? GetTickCount64() : 0;
        g_leftAimPoseValid = leftValid;
        if (leftValid)
            g_leftAimPose = leftLocation.pose;
        g_supportGripPoseValid = leftValid && physicalGripValid[0];
        if (g_supportGripPoseValid)
            g_supportGripPosePosition = physicalGripPosition[0];
        g_primaryGripPoseValid = valid && physicalGripValid[1];
        if (g_primaryGripPoseValid)
            g_primaryGripPosePosition = physicalGripPosition[1];
        g_supportGripPoseFresh.store(g_supportGripPoseValid, std::memory_order_release);
        g_leftAimLinearVelocityValid = selectedLeftVelocityValid;
        if (g_leftAimLinearVelocityValid)
            g_leftAimLinearVelocity = selectedLeftVelocity;
        g_leftAimLinearVelocityAtMs = g_leftAimLinearVelocityValid ? GetTickCount64() : 0;
        const auto recordMeleeSpeed = [](int hand, bool valid, const XrVector3f& velocity)
        {
            const float speed = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z);
            g_meleeSpeedHistory[hand].Update(
                GetTickCount64(), valid && (g_config.physical_melee || g_config.gesture_melee), speed);
        };
        recordMeleeSpeed(0, g_leftAimLinearVelocityValid, g_leftAimLinearVelocity);
        recordMeleeSpeed(1, g_rightAimLinearVelocityValid, g_rightAimLinearVelocity);
        LeaveCriticalSection(&g_headCs);
        static bool loggedPoseVelocityFallback = false;
        if ((rightUsedPoseDerived || leftUsedPoseDerived) && !loggedPoseVelocityFallback)
        {
            const XrVector3f& selected = rightUsedPoseDerived ? selectedRightVelocity : selectedLeftVelocity;
            const float selectedSpeed =
                std::sqrt(selected.x * selected.x + selected.y * selected.y + selected.z * selected.z);
            if (std::isfinite(selectedSpeed) && selectedSpeed >= 0.10f)
            {
                LOG("OpenXR controller velocity: pose-delta fallback active "
                    "because the runtime advertised a zero/missing linear "
                    "velocity; meaningful native velocity remains preferred");
                loggedPoseVelocityFallback = true;
            }
        }
        static bool logged = false;
        if (valid && !logged)
        {
            LOG("main-weapon controller tracking active pose=(%.3f,%.3f,%.3f)", location.pose.position.x,
                location.pose.position.y, location.pose.position.z);
            logged = true;
        }

        // Read the gamepad-replacement actions (already synced above).
        VrPadState pad{};
        auto getV2 = [&](XrAction action, float& outX, float& outY)
        {
            if (action == XR_NULL_HANDLE)
                return;
            XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
            gi.action = action;
            XrActionStateVector2f st{XR_TYPE_ACTION_STATE_VECTOR2F};
            if (XR_SUCCEEDED(xrGetActionStateVector2f(g_session, &gi, &st)) && st.isActive)
            {
                outX = st.currentState.x;
                outY = st.currentState.y;
                pad.valid = true;
            }
        };
        auto getF = [&](XrAction action, float& out)
        {
            if (action == XR_NULL_HANDLE)
                return false;
            XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
            gi.action = action;
            XrActionStateFloat st{XR_TYPE_ACTION_STATE_FLOAT};
            if (XR_SUCCEEDED(xrGetActionStateFloat(g_session, &gi, &st)) && st.isActive)
            {
                out = st.currentState;
                pad.valid = true;
                return true;
            }
            return false;
        };
        auto getB = [&](XrAction action, bool& out)
        {
            if (action == XR_NULL_HANDLE)
                return;
            XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
            gi.action = action;
            XrActionStateBoolean st{XR_TYPE_ACTION_STATE_BOOLEAN};
            if (XR_SUCCEEDED(xrGetActionStateBoolean(g_session, &gi, &st)) && st.isActive)
            {
                out = st.currentState == XR_TRUE;
                pad.valid = true;
            }
        };
        getV2(g_actMove, pad.moveX, pad.moveY);
        getV2(g_actTurn, pad.turnX, pad.turnY);
        const bool physicalLeftTriggerActive = getF(g_actTrigL, pad.trigL);
        const bool physicalRightTriggerActive = getF(g_actTrigR, pad.trigR);
        const bool supportGripActive = getF(g_actGripL, pad.gripL);
        const bool primaryGripActive = getF(g_actGripR, pad.gripR);
        g_controllerFingerSample = {g_preparedFrame.serial, time, g_contactSpaceEpoch.load(std::memory_order_acquire),
            leftHanded,
            {MakeControllerFingerInput(physicalLeftTriggerActive, pad.trigL, supportGripActive, pad.gripL),
                MakeControllerFingerInput(physicalRightTriggerActive, pad.trigR, primaryGripActive, pad.gripR)}};
        if (leftHanded)
        {
            std::swap(pad.trigL, pad.trigR);
            std::swap(pad.gripL, pad.gripR);
        }
        getB(g_actA, pad.a);
        getB(g_actB, pad.b);
        getB(g_actX, pad.x);
        getB(g_actY, pad.y);
        getB(g_actClickL, pad.clickL);
        getB(g_actClickR, pad.clickR);
        getB(g_actMenu, pad.menu);
        {
            float lx = 0, ly = 0, rx = 0, ry = 0;
            bool lc = false, rc = false;
            getV2(g_actFacePadL, lx, ly);
            getV2(g_actFacePadR, rx, ry);
            getB(g_actFaceClickL, lc);
            getB(g_actFaceClickR, rc);
            static vr_mapping::PadFaces faces[2];
            const auto leftProfile = g_controllerProfiles[0].load(std::memory_order_acquire);
            const auto rightProfile = g_controllerProfiles[1].load(std::memory_order_acquire);
            const bool leftPad = leftProfile == vr_mapping::Profile::Vive || leftProfile == vr_mapping::Profile::Wmr;
            const bool rightPad = rightProfile == vr_mapping::Profile::Vive || rightProfile == vr_mapping::Profile::Wmr;
            const int leftZone = faces[0].Update(ly, leftPad && lc);
            const int rightZone = faces[1].Update(ry, rightPad && rc);
            if (leftZone)
            {
                pad.x |= leftZone > 0;
                pad.y |= leftZone < 0;
            }
            if (rightZone)
            {
                pad.a |= rightZone > 0;
                pad.b |= rightZone < 0;
            }
            // Vive uses the same pad for locomotion and faces. A clicked
            // face zone owns those axes for its full hold; WMR retains its
            // physically separate thumbstick. Center clicks keep L3/R3.
            if (leftProfile == vr_mapping::Profile::Vive && leftZone)
            {
                pad.clickL = false;
                pad.moveX = pad.moveY = 0;
            }
            if (rightProfile == vr_mapping::Profile::Vive && rightZone)
            {
                pad.clickR = false;
                pad.turnX = pad.turnY = 0;
            }
        }
        const DpadStickInput thumbrest = ConsumeThumbrestDpad(g_config.quest_thumbrest_dpad, ReadLeftThumbrestTouched(),
            pad.valid && valid && leftValid && g_sessionState == XR_SESSION_STATE_FOCUSED && !Menu_IsOpen() &&
                !g_weaponInteraction.HasClaimedGrip(),
            pad.turnX, pad.turnY);
        pad.thumbrestDpad = thumbrest.active;
        pad.dpadX = thumbrest.x;
        pad.dpadY = thumbrest.y;
        static VrPadState previousPad{};
        static bool previousRawMenu = false;
        static uint64_t odstMenuPulseUntil = 0;
        const uint64_t inputNow = GetTickCount64();
        const bool rawMenuEdge = pad.menu && !previousRawMenu;
        if (rawMenuEdge)
        {
            LOG("controller edge: Menu/Start");
            if (Game_IsCameraOnlyBringup())
                odstMenuPulseUntil = inputNow + 350;
        }
        previousRawMenu = pad.menu;
        if (Game_IsCameraOnlyBringup() && inputNow < odstMenuPulseUntil)
            pad.menu = true;
        {
            // Save & Quit dialog keys (see VR_RequestPausePresentation). A resume press
            // is Menu/Start or B; Menu/Start also ends the dialog-key window.
            if (rawMenuEdge || (pad.b && !previousPad.b))
                g_lastResumeInputMs.store(inputNow, std::memory_order_release);
            if (rawMenuEdge)
                g_pauseEndedByGameMs.store(0, std::memory_order_release);
            static int lastDirX = 0;
            const int dirX =
                game_menu_pointer::StickDir(std::fabs(pad.moveX) > std::fabs(pad.dpadX) ? pad.moveX : pad.dpadX);
            const bool keysWindow =
                g_config.menu_dialog_keys && !Menu_IsOpen() &&
                game_menu_pointer::DialogKeysWindow(inputNow, g_pauseEndedByGameMs.load(std::memory_order_acquire));
            if (keysWindow && game_menu_pointer::GameplayInput(pad.trigR, pad.moveY))
            {
                // Play resumed (fire or walk): there is no dialog. Close the window.
                g_pauseEndedByGameMs.store(0, std::memory_order_release);
                LOG("menu keys: closed by gameplay input (trigger or walking)");
            }
            else if (keysWindow)
            {
                if (pad.a && !previousPad.a)
                    NativeMenuPointer_MenuKey(VK_RETURN, "A");
                if (dirX && dirX != lastDirX)
                    NativeMenuPointer_MenuKey(dirX < 0 ? VK_LEFT : VK_RIGHT, dirX < 0 ? "stick left" : "stick right");
            }
            lastDirX = dirX;
        }
        {
            // Diagnostic (Save & Quit dialog signal): Windows cursor visibility
            // changes, with MCC's runtime mode and pause view at that moment.
            // At most 8 change lines a second; the rest are counted.
            static int lastShown = -1;
            static unsigned cursorChanges = 0, cursorSuppressed = 0;
            static uint64_t cursorSecond = 0;
            if (inputNow - cursorSecond >= 1000)
            {
                if (cursorSuppressed)
                    LOG("MCC cursor: +%u visibility changes suppressed in the last second", cursorSuppressed);
                cursorSecond = inputNow;
                cursorChanges = 0;
                cursorSuppressed = 0;
            }
            CURSORINFO cursor{sizeof(cursor)};
            if (GetCursorInfo(&cursor))
            {
                const int shown = (cursor.flags & CURSOR_SHOWING) ? 1 : 0;
                if (shown != lastShown && ++cursorChanges > 8)
                {
                    lastShown = shown;
                    ++cursorSuppressed;
                }
                else if (shown != lastShown)
                {
                    lastShown = shown;
                    LOG("MCC cursor: %s at (%ld,%ld); runtime mode %u, pause view %d/%d, foreground in MCC process %d",
                        shown ? "SHOWN" : "hidden", cursor.ptScreenPos.x, cursor.ptScreenPos.y,
                        unsigned(TitleAdapter_GetRuntimeMode()), VR_IsPausePresentation() ? 1 : 0,
                        VR_IsPausePresentationTarget() ? 1 : 0,
                        []
                        {
                            DWORD pid = 0;
                            GetWindowThreadProcessId(GetForegroundWindow(), &pid);
                            return pid == GetCurrentProcessId() ? 1 : 0;
                        }());
                }
            }
        }
        {
            // Pad-feed diagnostic (Save & Quit dialog: the owner's buttons never reached us):
            // while paused, in a menu, or for 20 s after any pause ended, once a second
            // and on every change of valid/A/B/trigger/menu. The gameplay action set was
            // synced this frame (a failed sync logs separately and never gets here).
            const uint64_t feedNow = GetTickCount64();
            const uint64_t pauseEnd = g_lastPauseEndMs.load(std::memory_order_acquire);
            const bool feedWindow = VR_IsPausePresentation() || VR_IsPausePresentationTarget() ||
                                    game_menu_pointer::MenuMode(TitleAdapter_GetRuntimeMode(), false) ||
                                    (pauseEnd && feedNow >= pauseEnd && feedNow - pauseEnd <= 20000);
            // Windows foreground: VDXR may stop reporting controller actions while MCC
            // is not the foreground window (automated test runs always focus MCC first).
            const HWND feedFg = GetForegroundWindow();
            DWORD feedFgPid = 0;
            if (feedFg)
                GetWindowThreadProcessId(feedFg, &feedFgPid);
            const unsigned feedFgKind = !feedFg ? 0u : feedFgPid == GetCurrentProcessId() ? 1u : 2u; // none/MCC/other
            const unsigned feedKey = (pad.valid ? 1u : 0u) | (pad.a ? 2u : 0u) | (pad.b ? 4u : 0u) |
                                     (pad.trigR >= 0.5f ? 8u : 0u) | (pad.menu ? 16u : 0u) | (feedFgKind << 5);
            static unsigned lastFeedKey = ~0u;
            static uint64_t lastFeedLogMs = 0;
            // Change lines capped at 8 a second (a flapping input would otherwise log every frame).
            static uint64_t feedSecond = 0;
            static unsigned feedChanges = 0, feedSuppressed = 0;
            if (feedNow - feedSecond >= 1000)
            {
                if (feedSuppressed)
                    LOG("pad feed: +%u changes suppressed in the last second", feedSuppressed);
                feedSecond = feedNow;
                feedChanges = 0;
                feedSuppressed = 0;
            }
            bool feedCapped = false;
            if (feedWindow && feedKey != lastFeedKey && feedNow - lastFeedLogMs < 1000 && ++feedChanges > 8)
            {
                ++feedSuppressed;
                lastFeedKey = feedKey;
                feedCapped = true;
            }
            if (!feedCapped && feedWindow && (feedKey != lastFeedKey || feedNow - lastFeedLogMs >= 1000))
            {
                auto feedActive = [&](XrAction action) -> int
                {
                    if (action == XR_NULL_HANDLE)
                        return -1;
                    XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
                    gi.action = action;
                    if (action == g_actMove)
                    {
                        XrActionStateVector2f st{XR_TYPE_ACTION_STATE_VECTOR2F};
                        return XR_SUCCEEDED(xrGetActionStateVector2f(g_session, &gi, &st)) ? (st.isActive ? 1 : 0) : -2;
                    }
                    if (action == g_actTrigR)
                    {
                        XrActionStateFloat st{XR_TYPE_ACTION_STATE_FLOAT};
                        return XR_SUCCEEDED(xrGetActionStateFloat(g_session, &gi, &st)) ? (st.isActive ? 1 : 0) : -2;
                    }
                    XrActionStateBoolean st{XR_TYPE_ACTION_STATE_BOOLEAN};
                    return XR_SUCCEEDED(xrGetActionStateBoolean(g_session, &gi, &st)) ? (st.isActive ? 1 : 0) : -2;
                };
                const char* pointerFrame = "";
                const char* pointerDelivery = "";
                NativeMenuPointer_Status(pointerFrame, pointerDelivery);
                char fgTitle[80] = "", fgClass[64] = "";
                if (feedFg)
                {
                    wchar_t wide[80] = L"";
                    InternalGetWindowText(feedFg, wide, 80); // no WM_GETTEXT: never blocks on MCC's window thread
                    WideCharToMultiByte(CP_UTF8, 0, wide, -1, fgTitle, sizeof fgTitle, nullptr, nullptr);
                    GetClassNameA(feedFg, fgClass, sizeof fgClass);
                }
                LOG("pad feed: sync=gameplay ok session=%d valid=%d A=%d(active %d) B=%d(active %d) trigR=%.2f(active %d) "
                    "menu=%d(active %d) move=(%.2f,%.2f)(active %d) aimPose=%d mode=%u pause view=%d/%d pointer=%s/%s exclusive=%d "
                    "fg=%s \"%s\" class=%s",
                    int(g_sessionState), pad.valid ? 1 : 0, pad.a ? 1 : 0, feedActive(g_actA), pad.b ? 1 : 0,
                    feedActive(g_actB), pad.trigR, feedActive(g_actTrigR), pad.menu ? 1 : 0, feedActive(g_actMenu),
                    pad.moveX, pad.moveY, feedActive(g_actMove), valid ? 1 : 0, unsigned(TitleAdapter_GetRuntimeMode()),
                    VR_IsPausePresentation() ? 1 : 0, VR_IsPausePresentationTarget() ? 1 : 0, pointerFrame,
                    pointerDelivery, exclusive_input::active.load(std::memory_order_relaxed) ? 1 : 0,
                    feedFgKind == 0   ? "none"
                    : feedFgKind == 1 ? "MCC"
                                      : "other",
                    fgTitle, fgClass);
                lastFeedKey = feedKey;
                lastFeedLogMs = feedNow;
            }
        }
        if (pad.a && !previousPad.a)
            LOG("controller edge: A");
        if (pad.b && !previousPad.b)
            LOG("controller edge: B");
        if (pad.x && !previousPad.x)
            LOG("controller edge: X");
        if (pad.y && !previousPad.y)
            LOG("controller edge: Y");
        previousPad = pad;
        // Keep the pointer click private while suppressing all gameplay
        // consumers (including camera snapshots that bypass XInput). The VR
        // A button clicks at the laser too: sent as a gamepad A it made MCC
        // switch to controller mode and activate ITS highlighted item instead
        // of the one under the laser.
        g_nativePointerTrigger = std::max(pad.trigR, pad.a ? 1.0f : 0.0f);
        const bool pointerMode = g_config.game_menu_pointer && !Menu_IsOpen() &&
                                 g_sessionState == XR_SESSION_STATE_FOCUSED && valid &&
                                 NativeMenuPointer_ConsumesTrigger() &&
                                 game_menu_pointer::MenuMode(TitleAdapter_GetRuntimeMode(),
                                     VR_IsPausePresentation() && VR_IsPausePresentationTarget()) &&
                                 !VR_IsCutsceneTheaterActive();
        // The Menu button resumes an in-game pause while the laser owns
        // it, as Start does in flat MCC. Decided here on the raw edge: below, the
        // exclusive filter (consumedControls) clears every VR button but A, so
        // MergeVrPad never sees Menu. The press that paused came before the laser
        // owned input, so it is not a resume (ODST's 350 ms stretch is not an edge).
        if (game_menu_pointer::MenuResumesPause(
                pointerMode, VR_IsPausePresentation() && VR_IsPausePresentationTarget(), rawMenuEdge))
            Input_RequestPauseResume();
        // The optional thumb-rest D-pad turns only the right stick
        // into a D-pad (ConsumeThumbrestDpad above); fire, jump, grenade and movement stay
        // live. Before, it made the whole pad exclusive: a resting thumb could not shoot.
        pad.exclusiveInput = pointerMode;
        exclusive_input::active.store(pad.exclusiveInput || Menu_IsOpen(), std::memory_order_release);
        const bool f1Chord = pointerMode && pad.clickL && pad.clickR;
        static ExclusiveInputHolds consumedControls;
        // Include the raw physical right axes in the hold latch, even though
        // ConsumeThumbrestDpad already moved them to the D-pad fields.
        if (pad.thumbrestDpad && pad.exclusiveInput)
        {
            pad.turnX = pad.dpadX;
            pad.turnY = pad.dpadY;
        }
        consumedControls.ApplyVr(pad, pad.exclusiveInput, pointerMode && !pad.thumbrestDpad);
        if (pad.thumbrestDpad && !pad.exclusiveInput)
            pad.turnX = pad.turnY = 0.0f; // the stick is the D-pad
        if (f1Chord)
            pad.clickL = pad.clickR = true;
        const float rawSupportGrip = pad.gripL;
        const float rawPrimaryGrip = pad.gripR;
        weapon_interaction::Output weaponGesture{};
        bool bodySlotOwnsGrip = false;
        // The flashlight head touch and body slots publish through the same
        // validated gesture channel, so they keep the channel's sample fresh too.
        if (g_config.manual_reload || g_config.weapon_holsters || g_config.reach_grip_held ||
            g_config.flashlight_head_gesture || g_config.weapon_grenade_throw || g_config.weapon_grenade_type_slot ||
            g_config.weapon_equipment_slot || g_weaponInteraction.HasClaimedGrip() || g_grenadeThrow.OwnsTrigger() ||
            g_grenadeThrow.OwnsGrip())
        {
            // Use the same predicted-time head and controller space. This
            // optional extra locate does not change the late camera sample.
            XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
            constexpr XrSpaceLocationFlags tracked =
                XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
                XR_SPACE_LOCATION_POSITION_TRACKED_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
            const bool headValid = XR_SUCCEEDED(xrLocateSpace(g_viewSpace, g_localSpace, time, &head)) &&
                                   (head.locationFlags & tracked) == tracked && NormalizeTrackedPose(head.pose);
            weapon_interaction::Sample sample{};
            sample.now = inputNow;
            sample.title = TitleAdapter_GetActiveTitle();
            sample.generation = TitleAdapter_GetGeneration(sample.title);
            sample.space = g_contactSpaceEpoch.load(std::memory_order_acquire);
            sample.weaponGraph = VR_GetWeaponModelIdentity(sample.title, sample.generation, sample.space, inputNow);
            sample.dualWield = SecondaryWeaponPresentationActive();
            sample.ready = !pad.exclusiveInput && pad.valid && valid && leftValid && headValid && !handChanged &&
                           primaryGripActive && supportGripActive && (location.locationFlags & tracked) == tracked &&
                           (leftLocation.locationFlags & tracked) == tracked &&
                           g_sessionState == XR_SESSION_STATE_FOCUSED && Game_IsHeadTracking() &&
                           VR_IsStereoEnabled() && TitleAdapter_GetRuntimeMode() == RuntimeMode::Gameplay &&
                           !Menu_IsOpen() && !VR_IsPausePresentation() && !VR_IsPausePresentationTarget() &&
                           !VR_IsCutsceneTheaterActive();
            sample.head = {head.pose.position.x, head.pose.position.y, head.pose.position.z};
            sample.headRotation = {
                head.pose.orientation.x, head.pose.orientation.y, head.pose.orientation.z, head.pose.orientation.w};
            sample.primary = {location.pose.position.x, location.pose.position.y, location.pose.position.z};
            sample.primaryRotation = {location.pose.orientation.x, location.pose.orientation.y,
                location.pose.orientation.z, location.pose.orientation.w};
            sample.support = {leftLocation.pose.position.x, leftLocation.pose.position.y, leftLocation.pose.position.z};
            sample.supportRotation = {leftLocation.pose.orientation.x, leftLocation.pose.orientation.y,
                leftLocation.pose.orientation.z, leftLocation.pose.orientation.w};
            (void)g_reloadTargets.Read(sample, leftHanded);
            sample.primaryGrip = pad.gripR;
            sample.supportGrip = pad.gripL;
            sample.supportTrigger = pad.trigL;
            if (!primaryGripActive || !supportGripActive)
                sample.primaryGrip = sample.supportGrip = std::numeric_limits<float>::quiet_NaN();
            // The support trigger reaches the gesture as its own input: it takes
            // a magazine at the pouch, and counts as another action elsewhere.
            sample.otherAction = pad.trigR > 0.15f || pad.menu || pad.a || pad.b || pad.x || pad.y || pad.clickL ||
                                 pad.clickR || pad.thumbrestDpad;
            weapon_interaction::Settings settings{};
            settings.reload = g_config.manual_reload;
            settings.holsters = g_config.weapon_holsters;
            settings.leftHanded = leftHanded;
            settings.pouchDown = g_config.weapon_pouch_down_m;
            settings.pouchLocation = g_config.weapon_pouch_location;
            settings.pouchOffset = {
                g_config.weapon_pouch_offset_x_m, g_config.weapon_pouch_offset_y_m, g_config.weapon_pouch_offset_z_m};
            settings.zoneRadius = g_config.weapon_body_zone_radius_m;
            settings.holsterRadius = g_config.weapon_holster_radius_m;
            settings.insertRadius = g_config.weapon_insert_radius_m;
            settings.drawDistance = g_config.weapon_holster_draw_m;
            settings.holsterSlide = g_config.weapon_holster_slide;
            settings.holsterClick = g_config.weapon_holster_click;
            settings.needleShake = g_config.weapon_needler_shake;
            settings.genericVisual = g_config.weapon_unknown_reload_visual;
            settings.shakeTravel = g_config.weapon_shake_travel_m;
            settings.holsterLocation = g_config.weapon_holster_location;
            settings.bodyAnchor = g_config.weapon_zones_body_anchor;
            settings.zoneHaptic = g_config.weapon_holster_haptic;
            settings.zoneHapticRearmM = g_config.weapon_holster_haptic_rearm_m;
            settings.takeWithGrip = g_config.weapon_reload_take_grip;
            // The holster zone is where the RESERVE weapon is carried: one-handed
            // guns at the hip, long guns at the shoulder. An unknown reserve
            // (never held yet, unsupported title, no profile row) keeps the
            // configured location.
            // weapon_holster_by_class off: Unknown, so weapon_holster_location is honoured.
            weapon_profile::SizeClass reserveClass = weapon_profile::SizeClass::Unknown;
            if (g_config.weapon_holster_by_class)
            {
                static weapon_interaction::ReserveSlotCache reserveSlots;
                const uint64_t reserve = Game_GetReserveWeaponIdentity(sample.title, sample.generation);
                if (reserve)
                    reserveClass = weapon_profile::WeaponClassFor(sample.title, reserve);
                sample.reserveSlot = reserveSlots.Get(sample.title, reserve,
                    [&] { return weapon_interaction::SlotFor(weapon_profile::WeaponClassFor(sample.title, reserve)); });
            }
            else
                sample.reserveSlot = weapon_interaction::HolsterSlot::Unknown;
            if (g_config.weapon_holsters)
            {
                // Testable from the log, on change: "Held weapon: <title> primary <old>-><new>
                // identity <old>-><new>". A swap that really happened in game moves
                // the primary handle; a request that the game ignored does not.
                // Identity 0 (a non-catalog model such as Reach's arms) is no evidence.
                static GameTitle lastTitle = GameTitle::None;
                static uint32_t lastPrimary = 0xFFFFFFFFu;
                static uint64_t lastHeld = 0, lastLogMs = 0;
                Game_ReserveWeaponDiag d{};
                if (Game_GetReserveWeaponDiag(sample.title, d) && d.read && d.primary != 0xFFFFFFFFu)
                {
                    const uint64_t held = d.held ? d.held : lastHeld;
                    const bool changed = lastTitle != sample.title || d.primary != lastPrimary || held != lastHeld;
                    if (changed && inputNow - lastLogMs >= 250)
                    {
                        const int titleIndex = weapon_interaction::TitleIndex(sample.title);
                        LOG("Held weapon: %s primary %08X->%08X identity %016llX->%016llX",
                            titleIndex >= 0 ? weapon_interaction::kTitleNames[titleIndex] : "none",
                            lastTitle == sample.title ? lastPrimary : 0xFFFFFFFFu, d.primary,
                            (unsigned long long)(lastTitle == sample.title ? lastHeld : 0), (unsigned long long)held);
                        lastLogMs = inputNow;
                        lastTitle = sample.title;
                        lastPrimary = d.primary;
                        lastHeld = held;
                    }
                }
            }
            {
                // Testable from the log, on change: "Holster reserve: <title> class=<...> slot=<...>".
                static GameTitle loggedTitle = GameTitle::None;
                static weapon_profile::SizeClass loggedClass = weapon_profile::SizeClass::Unknown;
                static weapon_interaction::HolsterSlot loggedSlot = weapon_interaction::HolsterSlot::Unknown;
                static bool loggedAny = false;
                const int titleIndex = weapon_interaction::TitleIndex(sample.title);
                if (titleIndex >= 0 && g_config.weapon_holsters &&
                    (!loggedAny || loggedTitle != sample.title || loggedClass != reserveClass ||
                        loggedSlot != sample.reserveSlot))
                {
                    const char* className = reserveClass == weapon_profile::SizeClass::OneHanded ? "OneHanded"
                                            : reserveClass == weapon_profile::SizeClass::Long    ? "Long"
                                                                                                 : "Unknown";
                    const char* slotName = sample.reserveSlot == weapon_interaction::HolsterSlot::Hip ? "hip"
                                           : sample.reserveSlot == weapon_interaction::HolsterSlot::Shoulder
                                               ? "shoulder"
                                               : "default";
                    LOG("Holster reserve: %s class=%s slot=%s", weapon_interaction::kTitleNames[titleIndex], className,
                        slotName);
                    loggedAny = true;
                    loggedTitle = sample.title;
                    loggedClass = reserveClass;
                    loggedSlot = sample.reserveSlot;
                }
            }
            settings.reloadButton = WeaponGestureBinding(sample.title, vr_mapping::Reload, inputNow);
            settings.swapButton = WeaponGestureBinding(sample.title, vr_mapping::SwitchWeapon, inputNow);
            // Reach grip-held replaces the holster swap there (stow and grab-back, 9.4).
            const bool reachGripHeld = g_config.reach_grip_held && sample.title == GameTitle::HaloReach;
            const bool holsterGestures = settings.holsters;
            if (reachGripHeld)
                settings.holsters = false;
            weaponGesture = g_weaponInteraction.Update(sample, settings);
            settings.holsters = holsterGestures;
            if (reachGripHeld)
            {
                // The gun hand's raw grip, its evidence, and the holster zone it is in.
                pad.gunGripRaw = rawPrimaryGrip;
                pad.gunGripActive = primaryGripActive;
                pad.gunTracked = valid && (location.locationFlags & tracked) == tracked;
                pad.gunFocused = g_sessionState == XR_SESSION_STATE_FOCUSED;
                pad.gunReady = sample.ready;
                pad.gunSampleMs = inputNow;
                weapon_interaction::Vec forward = weaponGesture.bodyForward;
                const bool facing = weapon_interaction::Dot(forward, forward) > 0 ||
                                    weapon_interaction::HeadForward(sample.headRotation, forward);
                pad.gunZonesKnown = headValid && pad.gunTracked && facing && weapon_interaction::Finite(sample.head) &&
                                    weapon_interaction::Finite(sample.primary);
                pad.gunZone = holster::kNoZone;
                if (pad.gunZonesKnown)
                {
                    weapon_interaction::Vec shoulder{}, hip{};
                    weapon_interaction::HolsterPoints(sample.head, forward, settings, shoulder, hip);
                    const float radius = weapon_interaction::Setting(settings.holsterRadius, 0.08f, 0.40f, 0.20f);
                    const weapon_interaction::Vec toShoulder = sample.primary - shoulder, toHip = sample.primary - hip;
                    const float dShoulder = weapon_interaction::Dot(toShoulder, toShoulder),
                                dHip = weapon_interaction::Dot(toHip, toHip);
                    if (dShoulder <= radius * radius || dHip <= radius * radius)
                        pad.gunZone = dShoulder <= dHip ? holster::kShoulder : holster::kHip;
                }
            }
            // Off-hand grenade: shares the holsters' body basis; the magazine
            // pouch and an owned magazine trigger always win.
            grenade_throw::Output grenade{};
            {
                grenade_throw::Settings gs{};
                gs.enabled = g_config.weapon_grenade_throw;
                gs.typeSlot = g_config.weapon_grenade_type_slot;
                // Only Halo 3 equipment is a deployable item; Reach/H4 armor
                // abilities stay on the left stick click.
                gs.equipmentSlot = g_config.weapon_equipment_slot && sample.title == GameTitle::Halo3;
                gs.leftHanded = leftHanded;
                gs.pouchDown = settings.pouchDown;
                gs.equipmentButton =
                    gs.equipmentSlot ? WeaponGestureBinding(sample.title, vr_mapping::Equipment, inputNow) : 0;
                gs.slotRadius = g_config.weapon_grenade_slot_radius_m;
                gs.slotDown = g_config.weapon_grenade_slot_down_m;
                gs.throwSpeed = g_config.weapon_grenade_throw_speed;
                gs.haptic = g_config.weapon_grenade_haptic;
                gs.throwButton = WeaponGestureBinding(sample.title, vr_mapping::Grenade, inputNow);
                gs.cycleButton = WeaponGestureBinding(sample.title, vr_mapping::SwitchGrenade, inputNow);
                grenade_throw::Input gi{};
                gi.now = inputNow;
                gi.ready = sample.ready && !sample.dualWield;
                gi.head = sample.head;
                gi.support = sample.support;
                gi.trigger = sample.supportTrigger;
                gi.bodyForward = weaponGesture.bodyForward;
                if (weapon_interaction::Dot(gi.bodyForward, gi.bodyForward) <= 0)
                    (void)weapon_interaction::HeadForward(sample.headRotation, gi.bodyForward);
                weapon_interaction::Vec holsterUnused{};
                gi.pouchActive =
                    settings.reload &&
                    weapon_interaction::Zones(sample, settings, gi.pouch, holsterUnused,
                        weapon_interaction::Dot(gi.bodyForward, gi.bodyForward) > 0 ? &gi.bodyForward : nullptr);
                gi.pouchRadius = weapon_interaction::Setting(settings.zoneRadius, 0.08f, 0.40f, 0.20f);
                gi.reloadOwnsTrigger = weaponGesture.consumeSupportTrigger;
                gi.grip = rawSupportGrip;
                if (!supportGripActive)
                    gi.grip = std::numeric_limits<float>::quiet_NaN();
                gi.supportBusy = weaponGesture.holdingMagazine || weaponGesture.consumeSupport ||
                                 g_twoHandLatched.load(std::memory_order_acquire);
                grenade = g_grenadeThrow.Update(gi, gs);
                bodySlotOwnsGrip = grenade.consumeGrip;
            }
            const auto& supportQ = leftLocation.pose.orientation;
            g_weaponAccessory = weapon_accessory::Build(
                sample, settings, weaponGesture, {supportQ.x, supportQ.y, supportQ.z, supportQ.w});
            pad.weaponButtons = weaponGesture.buttons;
            pad.weaponSampleMs = inputNow;
            pad.weaponPulseUntilMs = weaponGesture.pulseUntil;
            pad.weaponTitle = sample.title;
            pad.weaponGeneration = sample.generation;
            pad.weaponSpace = sample.space;
            pad.weaponGraph = sample.weaponGraph;
            pad.weaponOptions =
                (settings.reload ? 1u : 0u) | (settings.holsters ? 2u : 0u) | (leftHanded ? 4u : 0u) |
                (!settings.holsterSlide ? 8u : 0u) | (settings.holsterClick ? 16u : 0u) |
                (settings.needleShake ? 32u : 0u) |
                (g_config.weapon_grenade_throw || g_config.weapon_grenade_type_slot || g_config.weapon_equipment_slot
                        ? 64u
                        : 0u) |
                (g_config.flashlight_head_gesture ? 128u : 0u);
            pad.weaponReloadBinding = settings.reloadButton;
            pad.weaponSwitchBinding = settings.swapButton;
            if (grenade.buttons)
            {
                pad.weaponButtons |= grenade.buttons;
                pad.weaponPulseUntilMs = std::max(pad.weaponPulseUntilMs, grenade.pulseUntil);
            }
            pad.weaponConsumePrimary = weaponGesture.consumePrimary;
            pad.weaponConsumeSupport = weaponGesture.consumeSupport;
            // Consume before publishing to ALL native/title consumers. A
            // cancelled/stale command must not resurrect its original bumper.
            if (pad.weaponConsumePrimary)
                pad.gripR = 0;
            if (pad.weaponConsumeSupport || grenade.consumeGrip)
                pad.gripL = 0;
            // A held magazine owns the support trigger: no grenade throw.
            if (weaponGesture.consumeSupportTrigger || grenade.consumeTrigger)
                pad.trigL = 0;
            if (grenade.haptic > 0)
                VR_PulseContactHaptics(true, grenade.haptic);
            if (grenade.thrown || grenade.cycled || grenade.took || grenade.deployed)
            {
                static uint64_t lastGrenadeLogMs = 0;
                if (inputNow - lastGrenadeLogMs >= 500)
                {
                    if (grenade.thrown)
                        LOG("Action fired: grenade via chest slot");
                    else if (grenade.cycled)
                        LOG("Action fired: switch grenade via back-hip slot");
                    else if (grenade.deployed)
                        LOG("Action fired: equipment via belt slot");
                    else
                        LOG("body slot: grenade take");
                    lastGrenadeLogMs = inputNow;
                }
            }
            if (weaponGesture.primaryHaptic > 0)
                VR_PulseContactHaptics(false, weaponGesture.primaryHaptic);
            if (weaponGesture.supportHaptic > 0)
                VR_PulseContactHaptics(true, weaponGesture.supportHaptic);
            if (weaponGesture.pickedMagazine || weaponGesture.grabbedHolster)
                g_weaponGestureGrabs.fetch_add(1, std::memory_order_relaxed);
            if (weaponGesture.reloadRequested)
            {
                g_weaponReloadRequests.fetch_add(1, std::memory_order_relaxed);
                if (!weapon_interaction::NeedleWeapon(sample.title, sample.weaponGraph))
                    (sample.receiverValid ? g_authoredInsertionRequests : g_fallbackInsertionRequests)
                        .fetch_add(1, std::memory_order_relaxed);
            }
            if (weaponGesture.swapRequested)
                g_weaponSwapRequests.fetch_add(1, std::memory_order_relaxed);
            g_weaponGestureReadyMs.store(sample.ready && !sample.dualWield ? inputNow : 0, std::memory_order_release);
        }
        else
        {
            g_weaponInteraction.Reset();
            g_grenadeThrow.Reset();
            g_weaponAccessory = {};
            g_weaponGestureReadyMs.store(0, std::memory_order_release);
        }
        g_scopeZoomStickY.store(pad.valid ? pad.turnY : 0.0f, std::memory_order_release);
        const bool supportWasLatched = g_twoHandLatched.load(std::memory_order_acquire);
        // Diagnostic-only pre-latch weapon probe (no gameplay effect).
        ProbeCaptureWeaponPreLatch();
        UpdateTwoHandLatch(valid, location.pose, leftValid, leftLocation.pose, rawSupportGrip, handChanged,
            weapon_interaction::BlocksSupportGrab(weaponGesture) || bodySlotOwnsGrip);
        UpdateTwoHandGrabFeedback(leftValid, rawSupportGrip);
        {
            // Fist melee: an open-air grip on an empty hand. Body slots,
            // holsters, magazines, the support grip and a second weapon keep
            // their grip and never make a fist.
            static fist_melee::Latch fists[2];
            const bool live = g_config.melee_fist && pad.valid && !pad.exclusiveInput && !handChanged;
            const bool supportClaimed = weaponGesture.consumeSupport || weaponGesture.holdingMagazine ||
                                        bodySlotOwnsGrip || supportWasLatched ||
                                        g_twoHandLatched.load(std::memory_order_acquire) ||
                                        SecondaryWeaponPresentationActive();
            const fist_melee::HandState support{live && leftValid, rawSupportGrip, supportClaimed};
            const fist_melee::HandState primary{live && valid, rawPrimaryGrip, weaponGesture.consumePrimary};
            g_bareFist[0].store(fist_melee::SupportStrikes(fists[0].Update(support), g_config.melee_support_free,
                                    live && leftValid, supportClaimed),
                std::memory_order_release);
            g_bareFist[1].store(fists[1].Update(primary), std::memory_order_release);
        }
        {
            static hud_reveal::HeadTouch reveal;
            static uint64_t flashlightPulseUntil = 0;
            static uint32_t flashlightPulse = 0;
            const GameTitle title = TitleAdapter_GetActiveTitle();
            const uint32_t generation = TitleAdapter_GetGeneration(title);
            const bool revealOn = g_config.hide_hud && g_config.hud_reveal_near_head;
            const bool flashlightOn = g_config.flashlight_head_gesture && !g_config.disable_flashlight_input;
            bool ready = (revealOn || flashlightOn) && pad.valid && valid && leftValid && !handChanged &&
                         !pad.exclusiveInput && g_sessionState == XR_SESSION_STATE_FOCUSED && Game_IsHeadTracking() &&
                         VR_IsStereoEnabled() && TitleAdapter_GetRuntimeMode() == RuntimeMode::Gameplay &&
                         !Menu_IsOpen() && !VR_IsPausePresentation() && !VR_IsPausePresentationTarget() &&
                         !VR_IsCutsceneTheaterActive() && !SecondaryWeaponPresentationActive() &&
                         !g_weaponInteraction.HasClaimedGrip() && !supportWasLatched &&
                         !g_twoHandLatched.load(std::memory_order_acquire) && rawSupportGrip < .15f && pad.trigL < .15f;
            float distance = 0;
            if (ready)
            {
                XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
                constexpr auto tracked = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
                ready = (leftLocation.locationFlags & tracked) == tracked &&
                        XR_SUCCEEDED(xrLocateSpace(g_viewSpace, g_localSpace, time, &head)) &&
                        (head.locationFlags & tracked) == tracked && NormalizeTrackedPose(head.pose);
                const auto& h = head.pose.position;
                const auto& p = leftLocation.pose.position;
                distance = std::sqrt((h.x - p.x) * (h.x - p.x) + (h.y - p.y) * (h.y - p.y) + (h.z - p.z) * (h.z - p.z));
            }
            // Title, generation and tracking-origin changes retire the dwell.
            const uint64_t epoch = (uint64_t(generation) << 32) ^ g_contactSpaceEpoch.load(std::memory_order_acquire) ^
                                   (uint64_t(static_cast<unsigned>(title)) << 56);
            // The head D-pad gesture on the same (physical left)
            // hand: decide the flashlight on release and stand down if the D-pad was used.
            const TitleTraits* gestureTraits = FindTitleTraits(title);
            const bool dpadShares = (gestureTraits && gestureTraits->dpadLeftHandBesideHead) || g_config.dpad_hand == 0;
            const uint64_t dpadUsedMs = Input_DpadGestureUsedMs();
            const bool dpadUsed = dpadUsedMs && inputNow >= dpadUsedMs && inputNow - dpadUsedMs < 150;
            const hud_reveal::HeadTouchResult touch = reveal.Update(inputNow, epoch, ready, distance,
                g_config.hud_reveal_radius_m, flashlightOn, revealOn, dpadShares, dpadUsed);
            g_hudReveal.Publish(touch.reveal, title, generation, inputNow);
            if (touch.flashlight)
            {
                // The game's own flashlight / VISR action, as one short press.
                flashlightPulse = WeaponGestureBinding(title, vr_mapping::Flashlight, inputNow);
                flashlightPulseUntil = flashlightPulse ? inputNow + 120 : 0;
                static uint64_t lastFlashlightLogMs = 0;
                if (flashlightPulse && inputNow - lastFlashlightLogMs >= 250)
                {
                    LOG("Action fired: flashlight via head touch");
                    lastFlashlightLogMs = inputNow;
                }
                if (flashlightPulse)
                    VR_PulseContactHaptics(true, feedback::Strength(feedback::Cue::FlashlightToggle));
            }
            if (flashlightPulse && inputNow < flashlightPulseUntil && !pad.exclusiveInput)
            {
                pad.weaponButtons |= flashlightPulse;
                pad.weaponPulseUntilMs = std::max(pad.weaponPulseUntilMs, flashlightPulseUntil);
            }
            else if (inputNow >= flashlightPulseUntil)
                flashlightPulse = 0;
        }
        EnterCriticalSection(&g_headCs);
        pad.profileEpoch = g_controllerProfileEpoch.load(std::memory_order_acquire);
        // The same grip edge that acquires OR releases support must not also
        // activate a flashlight. Publish this frame's decision, not last frame's.
        pad.supportAimActive = supportWasLatched || g_twoHandLatched.load(std::memory_order_acquire);
        g_padState = pad;
        g_thumbrestDpadSampleMs.store(pad.thumbrestDpad ? inputNow : 0, std::memory_order_release);
        LeaveCriticalSection(&g_headCs);
        ApplyControllerHaptics(valid, leftValid);
        static bool padLogged = false;
        if (pad.valid && !padLogged)
        {
            LOG("controller inputs active (sticks/buttons feeding the virtual gamepad)");
            padLogged = true;
        }
        return true;
    }
} // namespace vr_core
VirtualStockAimSettings VR_GetEffectiveVirtualStockAimSettings() noexcept
{
    return CurrentEffectiveVirtualStockAimSettings();
}
bool VR_GetHeadPose(float outQuat[4], float outPos[3])
{
    if (!g_headCsInit)
        return false;
    EnterCriticalSection(&g_headCs);
    const bool ok = g_headPoseValid;
    if (ok)
    {
        outQuat[0] = g_headPose.orientation.x;
        outQuat[1] = g_headPose.orientation.y;
        outQuat[2] = g_headPose.orientation.z;
        outQuat[3] = g_headPose.orientation.w;
        outPos[0] = g_headPose.position.x;
        outPos[1] = g_headPose.position.y;
        outPos[2] = g_headPose.position.z;
    }
    LeaveCriticalSection(&g_headCs);
    return ok;
}

const char* VR_ControllerProfileName(bool left)
{
    return vr_mapping::ProfileName(g_controllerProfiles[left ? 0 : 1].load(std::memory_order_acquire));
}
void VR_ReportWeaponInteractions(uint64_t nowMs)
{
    static HRESULT previousAccessory = S_FALSE;
    const HRESULT accessory = g_weaponAccessoryResult.load(std::memory_order_acquire);
    if (accessory != previousAccessory)
    {
        LOG("Reload accessory: %s HRESULT=0x%08X; isolated authored geometry and surface atlas, simple accessory lighting; "
            "native animation/ammo and camera unaffected",
            SUCCEEDED(accessory) ? "compositor draw ready" : "stock gesture fallback",
            static_cast<unsigned>(accessory));
        previousAccessory = accessory;
    }
    static uint64_t lastReport = 0;
    static GameTitle previousTitle = GameTitle::None;
    static unsigned previousOptions = 0, previousGrabs = 0, previousReloads = 0, previousSwaps = 0;
    static uint64_t previousBindings = 0;
    static bool previousReady = false;
    const GameTitle title = TitleAdapter_GetActiveTitle();
    const int index = weapon_interaction::TitleIndex(title);
    const unsigned options = (g_config.manual_reload ? 1u : 0u) | (g_config.weapon_holsters ? 2u : 0u) |
                             (g_config.left_handed ? 4u : 0u) | (!g_config.weapon_holster_slide ? 8u : 0u) |
                             (g_config.weapon_holster_click ? 16u : 0u) | (g_config.weapon_needler_shake ? 32u : 0u);
    const uint32_t reloadBinding = WeaponGestureBinding(title, vr_mapping::Reload, nowMs);
    const uint32_t switchBinding = WeaponGestureBinding(title, vr_mapping::SwitchWeapon, nowMs);
    const uint64_t bindings = uint64_t(reloadBinding) | (uint64_t(switchBinding) << 32);
    const bool changed = options != previousOptions || title != previousTitle || bindings != previousBindings;
    if (!changed && nowMs >= lastReport && nowMs - lastReport < 5000)
        return;
    const uint64_t readyAt = g_weaponGestureReadyMs.load(std::memory_order_acquire);
    const bool ready = readyAt && nowMs >= readyAt && nowMs - readyAt <= 150;
    static uint64_t previousModel = 0;
    const uint64_t modelIdentity = VR_GetWeaponModelIdentity(
        title, TitleAdapter_GetGeneration(title), g_contactSpaceEpoch.load(std::memory_order_acquire), nowMs);
    if ((options & 1) && (changed || previousModel != modelIdentity))
    {
        const auto* model = weapon_model::Find(title, modelIdentity);
        LOG("Reload model: title=%s identity=%016llX name=%s visual=%s needleShakeEligible=%u; "
            "native reload/ammo rules unchanged",
            index >= 0 ? weapon_interaction::kTitleNames[index] : "none",
            static_cast<unsigned long long>(modelIdentity),
            model ? model->name : (modelIdentity ? "unfamiliar held model" : "no fresh held-model receipt"),
            model ? (model->vertexCount ? "isolated authored part" : "gesture only")
                  : (modelIdentity && g_config.weapon_unknown_reload_visual ? "generic blue reload item"
                                                                            : "gesture only"),
            model && model->needles ? 1u : 0u);
    }
    previousModel = modelIdentity;
    const unsigned grabs = g_weaponGestureGrabs.load(std::memory_order_relaxed);
    const unsigned reloads = g_weaponReloadRequests.load(std::memory_order_relaxed);
    if ((options & 1) && (changed || reloads != previousReloads))
        LOG("Reload insertion receivers: authoredAssembly=%u genericControllerFallback=%u; "
            "fallback preserves the existing gesture when no fresh authored receiver is available",
            g_authoredInsertionRequests.load(std::memory_order_relaxed),
            g_fallbackInsertionRequests.load(std::memory_order_relaxed));
    const unsigned swaps = g_weaponSwapRequests.load(std::memory_order_relaxed);
    if (((options | previousOptions) & 3) && (changed || ready != previousReady || grabs != previousGrabs ||
                                                 reloads != previousReloads || swaps != previousSwaps))
        LOG("Weapon gestures: title=%s reload=%u holsters=%u leftHanded=%u readiness=%s "
            "configuredReload=0x%05X configuredSwitch=0x%05X grabs=%u reloadRequests=%u swapRequests=%u "
            "slide=%u click=%u pouchRadius=%.2f insertRadius=%.2f holsterRadius=%.2f drawMin=%.2f needleShake=%u "
            "(requests only; native ammo/inventory determine outcome)",
            index >= 0 ? weapon_interaction::kTitleNames[index] : "none", options & 1, (options >> 1) & 1,
            (options >> 2) & 1, ready ? "ready" : "stock input; waiting for focused tracked on-foot single-weapon play",
            reloadBinding, switchBinding, grabs, reloads, swaps, g_config.weapon_holster_slide ? 1u : 0u,
            g_config.weapon_holster_click ? 1u : 0u, g_config.weapon_body_zone_radius_m,
            g_config.weapon_insert_radius_m, g_config.weapon_holster_radius_m, g_config.weapon_holster_draw_m,
            g_config.weapon_needler_shake ? 1u : 0u);
    previousOptions = options;
    previousTitle = title;
    previousBindings = bindings;
    previousReady = ready;
    previousGrabs = grabs;
    previousReloads = reloads;
    previousSwaps = swaps;
    lastReport = nowMs;
}

void VR_SetGameHaptics(float amplitude)
{
    const float v = std::isfinite(amplitude) ? std::clamp(amplitude, 0.0f, 1.0f) : 0.0f;
    g_requestedHaptics.store(v, std::memory_order_release);
    // Peak-hold: raise the running peak so a pulse that arrives and clears
    // between two VR-frame samples still registers. Lock-free CAS max keeps
    // this XInput SetState hook thread hot-path-safe (no lock/alloc/logging).
    float cur = g_peakHaptics.load(std::memory_order_relaxed);
    while (
        v > cur && !g_peakHaptics.compare_exchange_weak(cur, v, std::memory_order_release, std::memory_order_relaxed))
    {
    }
}

uint32_t VR_InteractionProfileEpoch()
{
    return g_interactionProfileEpoch.load(std::memory_order_acquire);
}

void VR_PulseContactHaptics(bool left, float amplitude)
{
    // One master for every gesture/contact cue (F1 "Feedback cue strength").
    const float master =
        std::isfinite(g_config.feedback_cue_strength) ? std::clamp(g_config.feedback_cue_strength, 0.0f, 2.0f) : 1.0f;
    const float v = std::isfinite(amplitude) ? std::clamp(amplitude * master, 0.0f, 1.0f) : 0.0f;
    std::atomic<float>& peak = g_contactHaptics[left ? 0 : 1];
    float current = peak.load(std::memory_order_relaxed);
    while (v > current && !peak.compare_exchange_weak(current, v, std::memory_order_release, std::memory_order_relaxed))
    {
    }
}

uint64_t VR_WeaponHapticToken(GameTitle title, uint32_t generation, bool secondary) noexcept
{
    if (title != TitleAdapter_GetActiveTitle() || generation != TitleAdapter_GetGeneration(title))
        return 0;
    return g_weaponHapticAdmission.Token(secondary ? 0 : 1, GetTickCount64());
}
bool VR_PulseWeaponHaptics(GameTitle title, uint32_t generation, bool secondary, bool supported, float amplitude,
    uint64_t sourceToken) noexcept
{
    if (title != TitleAdapter_GetActiveTitle() || generation != TitleAdapter_GetGeneration(title))
        return false;
    const uint64_t now = GetTickCount64();
    if (sourceToken && sourceToken != g_weaponHapticAdmission.Token(secondary ? 0 : 1, now))
        return false;
    const uint64_t supportToken = sourceToken ? g_weaponHapticAdmission.Token(0, now) : 0;
    if (!g_weaponHaptics.Raise(title, generation, secondary ? 0 : 1, amplitude, now, sourceToken))
        return false;
    if (!secondary && supported && (!sourceToken || supportToken))
        (void)g_weaponHaptics.Raise(title, generation, 2, amplitude, now, sourceToken, supportToken);
    return true;
}

bool VR_GetPhysicalControllerPose(int hand, float outQuat[4], float outPos[3])
{
    if (!g_headCsInit || hand < 0 || hand > 1 || !outQuat || !outPos)
        return false;
    EnterCriticalSection(&g_headCs);
    const uint64_t now = GetTickCount64();
    const bool valid = g_physicalControllerValid[hand] && g_physicalControllerAtMs && now >= g_physicalControllerAtMs &&
                       now - g_physicalControllerAtMs <= 250;
    if (valid)
    {
        const auto& pose = g_physicalControllerPose[hand];
        outQuat[0] = pose.orientation.x;
        outQuat[1] = pose.orientation.y;
        outQuat[2] = pose.orientation.z;
        outQuat[3] = pose.orientation.w;
        outPos[0] = pose.position.x;
        outPos[1] = pose.position.y;
        outPos[2] = pose.position.z;
    }
    LeaveCriticalSection(&g_headCs);
    return valid;
}

bool VR_GetRightControllerPose(float outQuat[4], float outPos[3])
{
    if (!g_headCsInit)
        return false;
    EnterCriticalSection(&g_headCs);
    const bool ok = g_rightAimPoseValid;
    if (ok)
    {
        outQuat[0] = g_rightAimPose.orientation.x;
        outQuat[1] = g_rightAimPose.orientation.y;
        outQuat[2] = g_rightAimPose.orientation.z;
        outQuat[3] = g_rightAimPose.orientation.w;
        outPos[0] = g_rightAimPose.position.x;
        outPos[1] = g_rightAimPose.position.y;
        outPos[2] = g_rightAimPose.position.z;
    }
    LeaveCriticalSection(&g_headCs);
    return ok;
}

bool VR_GetPhysicalMeleeSpeed(bool left, float& speed)
{
    speed = -1.0f;
    if (!g_headCsInit)
        return false;
    EnterCriticalSection(&g_headCs);
    const bool valid = g_meleeSpeedHistory[left ? 0 : 1].Read(GetTickCount64(), speed);
    LeaveCriticalSection(&g_headCs);
    return valid;
}

bool VR_GetControllerLinearVelocity(bool left, float outVelocity[3])
{
    if (!g_headCsInit || !outVelocity)
        return false;
    EnterCriticalSection(&g_headCs);
    const uint64_t capturedAtMs = left ? g_leftAimLinearVelocityAtMs : g_rightAimLinearVelocityAtMs;
    const uint64_t nowMs = GetTickCount64();
    const bool ok = (left ? g_leftAimLinearVelocityValid : g_rightAimLinearVelocityValid) && capturedAtMs != 0 &&
                    nowMs >= capturedAtMs && nowMs - capturedAtMs <= 100;
    if (ok)
    {
        const XrVector3f& velocity = left ? g_leftAimLinearVelocity : g_rightAimLinearVelocity;
        outVelocity[0] = velocity.x;
        outVelocity[1] = velocity.y;
        outVelocity[2] = velocity.z;
    }
    LeaveCriticalSection(&g_headCs);
    return ok;
}

void VR_PublishSupportGripOwnerEvidence(GameTitle title, uint32_t generation, const support_grip::OwnerTuple& owner,
    support_grip::OwnerEvidence evidence) noexcept
{
    // One slot per title, one writer at a time inside it. A dropped
    // publication is a lost update, never a torn record; the producer's next
    // update republishes.
    (void)PublishSupportGripOwnerEvidenceInternal(title, generation, owner, evidence);
}

bool VR_SupportGripWiredForTitle(GameTitle title) noexcept
{
    // Transitional applicability gate: the feature may only touch a title
    // whose producer/trust/fallback slice is wired. PG off is always false.
    return g_config.persistent_support_grip && support_grip::PersistentSupportGripApplies(title);
}

bool VR_ReadSupportGripOwnerEvidence(GameTitle title, uint32_t expectedGeneration, support_grip::OwnerTuple& owner,
    support_grip::OwnerEvidence& evidence, uint64_t& revision) noexcept
{
    // The caller treats false as Unknown: it must never be read as a durable
    // release, and it can never bind an acquisition.
    return ReadSupportGripOwnerEvidenceInternal(title, expectedGeneration, owner, evidence, revision);
}

bool VR_GetSupportGripRelationship(SupportGripRelationshipSnapshot& snapshot) noexcept
{
    // False is "unavailable", not "disengaged": a caller that cannot prove the
    // reading must fail closed for support ownership.
    return ReadSupportGripRelationshipInternal(snapshot);
}
bool VR_IsTwoHandAiming()
{
    return g_twoHandActive.load() && !SecondaryWeaponPresentationActive();
}
bool VR_IsBareFist(int role)
{
    return role >= 0 && role < 2 && g_bareFist[role].load(std::memory_order_acquire);
}

bool VR_FingerPoseWanted()
{
    return g_config.experimental_body_ik || (g_config.physical_melee && g_config.melee_fist && VR_IsBareFist(0));
}
// The shared/base weapon-hand aim pose used by ALL aim consumers (bullet
// steering, the reticle, and the visible gun). Position is always the primary
// hand. Orientation is the primary controller's — UNLESS two-handed aim is
// engaged, in which case -Z is swung onto the two-hand line with roll kept
// from the primary controller. With the F1 Virtual stock option enabled, that
// line starts at the configured primary-to-head rear-reference blend and ends
// at the selected support endpoint; otherwise it is the primary -> support line. Downstream
// verified barrel-origin aiming (gun_barrel_aim) may substitute the muzzle
// origin/direction afterwards and is unaffected by this base pose.
bool VR_GetAimPoseWithSupportProvenance(
    float outQuat[4], float outPos[3], VrAimSupportReceipt& outReceipt, bool preferCommittedSample)
{
    outReceipt = VrAimSupportReceipt{};
    if (!g_headCsInit)
        return false;
    AimPoseInputs inputs{};
    // Hoisted out of the critical section: the transition packet must only be
    // applied to a live pose assembled the same way the seam assembled its live
    // pose, and only for the prepared serial those poses belong to (see
    // PresentAimContinuityFromPublication).
    bool okH = false;
    // Serial of the prepared frame whose poses this call is about to read. A
    // live assembly is re-checked after reading each packet; a selected
    // committed sample retains this exact identity if prepare advances mid-call.
    const uint64_t expectedSerial = g_preparedSerialPublished.load(std::memory_order_acquire);
    bool usedCommittedSample = false;
    XrVector3f headPosition{};
    XrQuaternionf headOrientation{};
    if (preferCommittedSample)
    {
        const GameTitle title = TitleAdapter_GetActiveTitle();
        const committed_aim::Identity current{expectedSerial, g_contactSpaceEpoch.load(std::memory_order_acquire),
            g_framePacingSessionEpoch.load(std::memory_order_acquire), title, TitleAdapter_GetGeneration(title),
            g_capturedLeftHanded.load(std::memory_order_acquire)};
        committed_aim::Sample committed{};
        if (committed_aim::TrySelectCurrent(
                g_committedAimSample, preferCommittedSample, current, GetTickCount64(), committed))
        {
            const auto& values = committed.values;
            okH = values.okH;
            headPosition = values.headPose.position;
            headOrientation = values.headPose.orientation;
            inputs = CurrentStockAimPoseInputsWithNeutralCapture(values.okR, values.rightAimPose, values.okL,
                values.leftAimPose, okH, headPosition, headOrientation, values.supportGripValid,
                values.supportGripPosition, values.primaryGripValid, values.primaryGripPosition,
                values.inverseNeckNeutralValid, values.inverseNeckNeutralOrientation,
                values.inverseNeckNeutralCaptureSerial, values.inverseNeckNeutralCaptureContactSpaceEpoch);
            committed_aim::ApplyAimValuesToSolverInputs(inputs, values);
            inputs.supportSolveSerial = committed.preparedSerial;
            usedCommittedSample = true;
        }
    }
    if (!usedCommittedSample)
    {
        // Preserve the original live-globals capture path exactly for OFF and
        // for any absent, torn, expired or identity-mismatched publication.
        EnterCriticalSection(&g_headCs);
        const bool okR = g_rightAimPoseValid;
        const XrPosef right = g_rightAimPose;
        const bool okL = g_leftAimPoseValid;
        const XrPosef left = g_leftAimPose;
        okH = g_headPoseValid && g_stockAimFresh.load(std::memory_order_acquire);
        headPosition = g_headPose.position;
        headOrientation = g_headPose.orientation;
        const bool supportGripFresh = g_supportGripPoseValid && g_supportGripPoseFresh.load(std::memory_order_acquire);
        const XrVector3f supportGrip = g_supportGripPosePosition;
        // Same-sample primary grip under this same hold (see the Lab store
        // proof): the success-path capture wrote it with these AIM poses, and
        // the only concurrent writer clears toward fail-open.
        const bool primaryGripValid = g_primaryGripPoseValid;
        const XrVector3f primaryGrip = g_primaryGripPosePosition;
        inputs = CurrentStockAimPoseInputs(okR, right, okL, left, okH, headPosition, headOrientation, supportGripFresh,
            supportGrip, primaryGripValid, primaryGrip);
        LeaveCriticalSection(&g_headCs);
    }

    // Cross-thread consumers use only the exact prepared serial's immutable
    // publication, including the strength/mix it was frozen with. A torn,
    // stale, inactive or zero-strength packet leaves the solver on raw input.
    // The eligibility terms are exactly the frame-thread ones
    // (ApplyPreparedTwoHandInputSmoothing) so one serial can never be consumed
    // differently on the two threads; Virtual Stock on or off is accepted, and
    // the read below re-checks every identity term it was published with.
    TwoHandInputSmoothingPreparedOutput smoothing{};
    const GameTitle smoothingTitle = TitleAdapter_GetActiveTitle();
    const uint32_t smoothingGeneration = TitleAdapter_GetGeneration(smoothingTitle);
    const uint64_t smoothingEpoch = g_contactSpaceEpoch.load(std::memory_order_acquire);
    if (inputs.twoHandEnabled && inputs.twoHandLatched && inputs.leftValid &&
        ReadTwoHandInputSmoothing(expectedSerial, smoothingEpoch, smoothingTitle, smoothingGeneration,
            inputs.virtualStockLeftHanded, inputs.supportEndpointUsedGrip, smoothing, usedCommittedSample) &&
        smoothing.strength > 0.0f)
    {
        ApplyTwoHandSmoothingGeometry(inputs, smoothing.mixed);
    }

    const AimPoseResult aim = ComputeAimPose(inputs);
    if (!aim.updateTwoHandActivity)
        return false;

    // Grab/release aim continuity (Virtual Stock, Standard and Plus): only the
    // emitted orientation may change, and only while a correction is active
    // for a valid published packet that belongs to this prepared serial and
    // was assembled from the same solver inputs. Position, validation, the
    // two-hand activity indicator and the logging below are byte-for-byte
    // unchanged.
    const XrQuaternionf presentedOrientation = PresentAimContinuityFromPublication(
        aim.pose.orientation, expectedSerial, okH, inputs.supportEndpointUsedGrip, usedCommittedSample);
    // Two-Hand Lab temporal (VS-off only): runs after the VS presentation
    // above through its own lock-free packet. The packet carries the serial,
    // epoch, settings generation and stateless orientation of the solve its
    // correction belongs to; anything else fails open to the orientation
    // above. Both inactive is identity.
    XrQuaternionf labPresentedOrientation = PresentTwoHandLabTemporalFromPublication(
        presentedOrientation, expectedSerial, inputs.twoHandLabGeneration, usedCommittedSample);

    // Preserve the getter's existing output contract: once the right pose is
    // valid, publish the best pose even if final quaternion validation fails.
    {
        // While zoomed, steady the aim (and so the zoom view) by zoom level.
        float smoothed[4]{
            labPresentedOrientation.x, labPresentedOrientation.y, labPresentedOrientation.z, labPresentedOrientation.w};
        const float amount = zoom_smoothing::AmountFor(g_scopeRuntimeZoom.load(std::memory_order_acquire), 6.0f, 24.0f,
            g_config.zoom_smoothing_low, g_config.zoom_smoothing_high);
        const bool zoomed = g_scopeActive.load(std::memory_order_acquire);
        if (zoomed || !g_zoomSmootherIdle.load(std::memory_order_acquire))
        {
            // Only zoomed frames (and the one reset after) take the lock.
            std::lock_guard<std::mutex> lock(g_zoomSmoothingMutex);
            g_zoomSmoother.Apply(GetTickCount64(), zoomed, amount, smoothed);
            // Forget the eased pose when a zoom ends, so a quick re-zoom starts fresh.
            if (!zoomed)
                g_zoomSmoother.Reset();
            g_zoomSmootherIdle.store(!zoomed, std::memory_order_release);
        }
        labPresentedOrientation = {smoothed[0], smoothed[1], smoothed[2], smoothed[3]};
    }
    outQuat[0] = labPresentedOrientation.x;
    outQuat[1] = labPresentedOrientation.y;
    outQuat[2] = labPresentedOrientation.z;
    outQuat[3] = labPresentedOrientation.w;
    outPos[0] = aim.pose.position.x;
    outPos[1] = aim.pose.position.y;
    outPos[2] = aim.pose.position.z;

    if (aim.twoHandActive)
    {
        const bool wasActive = g_twoHandActive.exchange(true);
        if (!wasActive)
            LOG("two-handed aim engaged (left grip held, hand on barrel)");
        // Diagnostic: how far the published aim sits off the rear-hand -> support-hand
        // line (grip poses, the points the solver uses). Measured once the
        // 200 ms grab continuity ease has finished, not on the engage frame.
        static uint64_t lastLineLogMs = 0, engagedAtMs = 0;
        const uint64_t now = GetTickCount64();
        if (!wasActive)
            engagedAtMs = now;
        if (inputs.primaryGripValid && inputs.supportGripValid && now - engagedAtMs >= 250 &&
            now - lastLineLogMs >= 2000)
        {
            const XrVector3f a = inputs.primaryGripPosition, b = inputs.supportGripPosition;
            float line[3]{b.x - a.x, b.y - a.y, b.z - a.z};
            const float length = sqrtf(line[0] * line[0] + line[1] * line[1] + line[2] * line[2]);
            const float x = outQuat[0], y = outQuat[1], z = outQuat[2], w = outQuat[3];
            const float forward[3]{-2.0f * (x * z + w * y), -2.0f * (y * z - w * x), -(1.0f - 2.0f * (x * x + y * y))};
            if (std::isfinite(length) && length > 0.05f)
            {
                const float dot = std::clamp(
                    (line[0] * forward[0] + line[1] * forward[1] + line[2] * forward[2]) / length, -1.0f, 1.0f);
                LOG("two-hand aim: published aim is %.1f deg off the hand line (exact_line=%d influence=%.2f)",
                    acosf(dot) * 57.2957795f, g_config.two_hand_exact_line ? 1 : 0,
                    g_config.two_hand_exact_line ? 1.0f : g_config.two_hand_offhand_influence);
                lastLineLogMs = now;
            }
        }
    }
    else
    {
        ClearTwoHandActive();
    }
    if (aim.rejectedExtreme)
    {
        static uint64_t lastRejectLogMs = 0;
        const uint64_t now = GetTickCount64();
        if (now - lastRejectLogMs >= 2000)
        {
            LOG("rejected extreme two-hand aim (ray agreement %.2f); "
                "using right controller",
                aim.rejectedAgreement);
            lastRejectLogMs = now;
        }
    }
    // The frozen solve-time receipt travels with the returned pose. The
    // orientation above may carry the VS/Lab presentation corrections, which
    // are owner-invariant by construction (they compose onto the same live
    // solve), so the receipt still describes the pose's support authority.
    outReceipt.supportEpoch = aim.supportEpoch;
    outReceipt.supportTrusted = aim.supportTrusted;
    outReceipt.relationshipReadable = aim.supportRelationshipReadable;
    outReceipt.relationshipEngaged = aim.supportRelationshipEngaged;
    outReceipt.solveSerial = aim.supportSolveSerial;
    outReceipt.headValid = okH;
    outReceipt.headPosition = headPosition;
    outReceipt.headOrientation = headOrientation;
    if (!aim.valid)
        return false;
    return true;
}

bool VR_GetAimPose(float outQuat[4], float outPos[3])
{
    VrAimSupportReceipt receipt{};
    return VR_GetAimPoseWithSupportProvenance(outQuat, outPos, receipt);
}

bool VR_GetLeftControllerPose(float outQuat[4], float outPos[3])
{
    if (!g_headCsInit)
        return false;
    EnterCriticalSection(&g_headCs);
    const bool ok = g_leftAimPoseValid;
    if (ok)
    {
        outQuat[0] = g_leftAimPose.orientation.x;
        outQuat[1] = g_leftAimPose.orientation.y;
        outQuat[2] = g_leftAimPose.orientation.z;
        outQuat[3] = g_leftAimPose.orientation.w;
        outPos[0] = g_leftAimPose.position.x;
        outPos[1] = g_leftAimPose.position.y;
        outPos[2] = g_leftAimPose.position.z;
    }
    LeaveCriticalSection(&g_headCs);
    return ok;
}
