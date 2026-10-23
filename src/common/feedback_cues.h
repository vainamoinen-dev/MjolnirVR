#pragma once
// Haptic feedback cue table for gestures and contacts.
// Used by dll/core/vr.h, support_grab_logic.h, weapon_interaction_logic.h and the contact melee tests.
// Constant data and pure scaling only.
//
// One table for every gesture and contact feedback cue (feedback plan: a tick on
// every state change). Values are the tuned defaults the features already
// used, so feel is unchanged; the F1 "Feedback cue strength" master
// (feedback_cue_strength, 1 = these values) scales all of them in one place
// (VR_PulseContactHaptics). Per-feature sliders (holster tick, two-hand grab,
// grenade haptic) still scale their own cues on top.
//
// Game-driven weapon haptics (firing, damage) are not cues and keep their own
// haptic_intensity setting. Empty-gun click and low ammo have no cue: no ammo
// reader exists without new hooks.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace feedback
{
    enum class Cue : uint8_t
    {
        HolsterEnter,     // weapon hand enters the holster (per-feature slider default)
        HolsterGrab,      // grip closes in the holster (slide mode)
        WeaponSwap,       // draw/stow: the native switch-weapon is sent
        TwoHandGrab,      // support hand takes the barrel (per-feature slider default)
        TwoHandRelease,   // deliberate let-go of the barrel
        MagTake,          // magazine taken from the pouch
        MagSeatSupport,   // magazine seats: support hand
        MagSeatWeapon,    // magazine seats: weapon hand
        ShakeReload,      // needle-weapon shake reload
        GrenadeTake,      // grenade taken from the chest slot
        GrenadeThrow,     // grenade thrown
        GrenadeSwitch,    // grenade type switched at the back hip
        EquipmentTake,    // Halo 3 equipment taken from the belt
        EquipmentDeploy,  // equipment tossed / dropped
        FlashlightToggle, // head-touch flashlight / VISR
        MeleeHit,         // contact melee lands (H2, H3, ODST, Reach, H4)
        MeleeHitCE,       // contact melee lands (CE)
        WorldContact,     // hand or gun touches the world (world collision)
        WheelGrab,        // a hand takes the steering wheel
        WheelRelease,     // a hand lets go of the wheel
        GrabPickupPrompt, // Reach: a weapon is under the reaching support hand
        Count
    };

    struct CueSpec
    {
        Cue cue;
        const char* name;
        float strength;
    };
    inline constexpr CueSpec kCues[]{
        {Cue::HolsterEnter, "holster enter", 0.12f},
        {Cue::HolsterGrab, "holster grab", 0.20f},
        {Cue::WeaponSwap, "weapon swap", 0.45f},
        {Cue::TwoHandGrab, "two-hand grab", 0.20f},
        {Cue::TwoHandRelease, "two-hand release", 0.10f},
        {Cue::MagTake, "magazine take", 0.20f},
        {Cue::MagSeatSupport, "magazine seat (support)", 0.45f},
        {Cue::MagSeatWeapon, "magazine seat (weapon)", 0.20f},
        {Cue::ShakeReload, "shake reload", 0.45f},
        {Cue::GrenadeTake, "grenade take", 0.20f},
        {Cue::GrenadeThrow, "grenade throw", 0.40f},
        {Cue::GrenadeSwitch, "grenade switch", 0.15f},
        {Cue::EquipmentTake, "equipment take", 0.20f},
        {Cue::EquipmentDeploy, "equipment deploy", 0.35f},
        {Cue::FlashlightToggle, "flashlight toggle", 0.15f},
        {Cue::MeleeHit, "melee hit", 0.65f},
        {Cue::MeleeHitCE, "melee hit (CE)", 0.35f},
        {Cue::WorldContact, "world contact", 0.18f},
        {Cue::WheelGrab, "wheel grab", 0.30f},
        {Cue::WheelRelease, "wheel release", 0.15f},
        {Cue::GrabPickupPrompt, "weapon under hand", 0.20f},
    };
    static_assert(
        sizeof(kCues) / sizeof(kCues[0]) == static_cast<size_t>(Cue::Count), "every cue has exactly one table row");

    inline constexpr float Strength(Cue cue) noexcept
    {
        for (const CueSpec& spec : kCues)
            if (spec.cue == cue)
                return spec.strength;
        return 0.0f;
    }

    inline constexpr float kMasterMinimum = 0.0f, kMasterMaximum = 2.0f, kMasterDefault = 1.0f;

    // Applies the F1 master to one cue amplitude; never above full strength.
    inline float Scaled(float amplitude, float master) noexcept
    {
        const float m = std::isfinite(master) ? std::clamp(master, kMasterMinimum, kMasterMaximum) : kMasterDefault;
        const float a = std::isfinite(amplitude) ? amplitude : 0.0f;
        return std::clamp(a * m, 0.0f, 1.0f);
    }
} // namespace feedback
