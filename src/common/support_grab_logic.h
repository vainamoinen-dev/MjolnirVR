#pragma once
// Support-grab acquisition: whether the support hand is inside a gun's grab zone this frame.
// Used by weapon_profiles.h and src/dll/core/vr.cpp; tested by tests/support_grab_tests.cpp.
// Pure geometry: no globals, no locks, no logging, no OpenXR types.
//
// Pure support-grab acquisition predicate for the persistent support grip
// (src/dll/core/vr.cpp UpdatePersistentSupportGrip, reached only while
// VR_SupportGripWiredForTitle() is true: `persistent_support_grip` on and a
// wired title). With the feature off (or a title not wired) this rule is not
// consulted at all: the shared latch path keeps the base acquisition geometry
// verbatim (palm-depth shift along the support controller's own forward).
//
// The palm-depth calibration offset (left_grip_forward_m, default 0.097 m,
// clamp [-0.05, 0.25] -- all unchanged) shifts the acquisition sample off the
// tracked support point toward the visible palm. That shift is expressed
// along the stable primary acquisition axis, not along the support
// controller's own forward vector.
//
// The support controller's own orientation is deliberately NOT an input
// anywhere in this header. The old shift orbited the effective sample by up
// to the full offset (+/- 9.7 cm default) around a 9 cm lateral acceptance
// radius, so rotating the support wrist alone flipped grab eligibility. With
// the shift on the shared acquisition axis, same support-hand position plus a
// different wrist orientation yields the same sample, while moving the hand
// still moves along/lateral naturally. There is no Virtual Stock mode,
// authority or stock state here: acquisition independence from the Virtual
// Stock setting is by construction, not by a branch.
//
// The zone arithmetic is otherwise exactly the pre-fix acceptance rule:
// primary-anchored origin nudged along the primary right vector, strict
// along/lateral thresholds, fail closed on non-finite geometry.
//
// Pure geometry only: no globals, no locks, no logging, no OpenXR types, so
// the standalone unit-test target exercises this exact code.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "virtual_stock_logic.h"
#include "feedback_cues.h"

namespace support_grab
{
    // Exact pre-fix acceptance values. These are product defaults; changing one
    // is a separate evidence-backed candidate, never a unit-test convenience.
    inline constexpr float kAlongMinimumM = 0.08f;
    inline constexpr float kAlongMaximumM = 0.80f;
    inline constexpr float kLateralMaximumM = 0.09f;
    inline constexpr float kZoneRightClampM = 0.10f;

    struct SupportGrabZone
    {
        float along = 0.0f;
        float lateral = 0.0f;
        bool inZone = false;
    };

    // Acceptance thresholds. Defaults are the exact pre-fix values above; a
    // per-weapon profile (weapon_profile::GrabZoneFor) may supply its own.
    struct ZoneLimits
    {
        float alongMinM = kAlongMinimumM;
        float alongMaxM = kAlongMaximumM;
        float lateralMaxM = kLateralMaximumM;
    };
    inline constexpr float kExtraRadiusMaximumM = 0.10f;

    // The F1 "extra grab radius" widens the tube sideways and lengthens it at both
    // ends by the same margin. 0 (the default) keeps the limits exactly. A
    // non-finite or inverted input falls back to the default limits.
    inline ZoneLimits WidenedZone(ZoneLimits limits, float extraM) noexcept
    {
        if (!std::isfinite(limits.alongMinM) || !std::isfinite(limits.alongMaxM) ||
            !std::isfinite(limits.lateralMaxM) || limits.alongMaxM <= limits.alongMinM || limits.lateralMaxM <= 0.0f)
            limits = ZoneLimits{};
        const float extra = std::isfinite(extraM) ? std::clamp(extraM, 0.0f, kExtraRadiusMaximumM) : 0.0f;
        limits.alongMinM = std::max(0.0f, limits.alongMinM - extra);
        limits.alongMaxM += extra;
        limits.lateralMaxM += extra;
        return limits;
    }

    // Palm-depth sample on the acquisition axis. Positive gripForwardM moves
    // toward the barrel; the configured value, its [-0.05, 0.25] clamp and the
    // non-finite fail-open behavior stay with the existing
    // virtual_stock::SupportGrabPoint helper (not modified by this header).
    inline virtual_stock::Point3 SupportGrabSample(
        virtual_stock::Point3 rawSupport, virtual_stock::Point3 acquisitionForward, float gripForwardM) noexcept
    {
        return virtual_stock::SupportGrabPoint(rawSupport, acquisitionForward, gripForwardM);
    }

    // Acquisition zone in the primary controller's frame. `zoneRightM` is the F1
    // side nudge of the zone origin along the primary right vector, clamped to
    // +/- kZoneRightClampM. Non-finite geometry fails closed (inZone = false,
    // zeroed along/lateral) and never relocates the zone.
    inline SupportGrabZone EvaluateSupportGrabZone(virtual_stock::Point3 primaryPosition,
        virtual_stock::Point3 acquisitionForward, virtual_stock::Point3 primaryRight, float zoneRightM,
        virtual_stock::Point3 rawSupportPosition, float gripForwardM, ZoneLimits limits = {}) noexcept
    {
        SupportGrabZone zone{};
        if (!virtual_stock::Finite(primaryPosition) || !virtual_stock::Finite(acquisitionForward) ||
            !virtual_stock::Finite(primaryRight) || !virtual_stock::Finite(rawSupportPosition) ||
            !std::isfinite(zoneRightM) || !std::isfinite(gripForwardM))
            return zone;

        const float zoneRight = std::clamp(zoneRightM, -kZoneRightClampM, kZoneRightClampM);
        if (!std::isfinite(zoneRight))
            return zone;
        const virtual_stock::Point3 sample = SupportGrabSample(rawSupportPosition, acquisitionForward, gripForwardM);
        if (!virtual_stock::Finite(sample))
            return zone;

        const virtual_stock::Point3 origin{primaryPosition.x + primaryRight.x * zoneRight,
            primaryPosition.y + primaryRight.y * zoneRight, primaryPosition.z + primaryRight.z * zoneRight};
        if (!virtual_stock::Finite(origin))
            return zone;

        const virtual_stock::Point3 v{sample.x - origin.x, sample.y - origin.y, sample.z - origin.z};
        const float along = virtual_stock::Dot(v, acquisitionForward);
        const virtual_stock::Point3 perpendicular{
            v.x - along * acquisitionForward.x, v.y - along * acquisitionForward.y, v.z - along * acquisitionForward.z};
        const float lateralSquared = virtual_stock::Dot(perpendicular, perpendicular);
        if (!virtual_stock::Finite(v) || !virtual_stock::Finite(perpendicular) || !std::isfinite(along) ||
            !std::isfinite(lateralSquared) || lateralSquared < 0.0f)
            return zone;
        const float lateral = std::sqrt(lateralSquared);
        if (!std::isfinite(lateral))
            return zone;

        zone.along = along;
        zone.lateral = lateral;
        zone.inZone = along > limits.alongMinM && along < limits.alongMaxM && lateral < limits.lateralMaxM;
        return zone;
    }

    // Support-hand haptic tick when the two-hand grab engages, and a softer one
    // (half strength) when the player lets go. A release the player did not make
    // (tracking loss, weapon gesture, title reset) stays silent. Returns this
    // frame's amplitude; 0 strength disables both.
    // Per-weapon limits change only when the held weapon (or the extra radius)
    // does, so the profile lookup runs once per change, not every frame.
    struct ZoneLimitsCache
    {
        template <class Lookup>
        ZoneLimits Get(int title, uint64_t identity, float extraM, Lookup lookup)
        {
            if (!valid_ || title != title_ || identity != identity_ ||
                !(extraM == extra_)) // NaN never matches: re-resolve (and fall back)
            {
                limits_ = WidenedZone(lookup(), extraM);
                title_ = title;
                identity_ = identity;
                extra_ = extraM;
                valid_ = true;
            }
            return limits_;
        }
        void Reset() noexcept { valid_ = false; }

    private:
        ZoneLimits limits_{};
        uint64_t identity_ = 0;
        float extra_ = 0.0f;
        int title_ = 0;
        bool valid_ = false;
    };

    class GrabFeedback
    {
    public:
        void Reset() noexcept { engaged_ = false; }
        float Update(bool engaged, bool deliberateRelease, float strength) noexcept
        {
            const float s = std::isfinite(strength) ? std::clamp(strength, 0.0f, 1.0f) : 0.0f;
            float out = 0.0f;
            if (engaged && !engaged_)
                out = s;
            else if (!engaged && engaged_ && deliberateRelease)
                out = s * (feedback::Strength(feedback::Cue::TwoHandRelease) /
                              feedback::Strength(feedback::Cue::TwoHandGrab));
            engaged_ = engaged;
            return out;
        }

    private:
        bool engaged_ = false;
    };
} // namespace support_grab
