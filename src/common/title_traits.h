#pragma once
// Per-title data the shared VR code consults, one row per supported title.
// The core reads behaviour differences from here instead of comparing title
// names; a title's row is its data adapter (hooks and engine readers live in
// the title's own code). Rows only hold constant data.
// Used by src/dll/core (input.cpp, vr.cpp); tested by tests/title_traits_tests.cpp.
#include <cstddef>

#include "authored_reticle_logic.h"
#include "runtime_types.h"

struct TitleTraits
{
    GameTitle title;

    // Authored crosshair publication (SubmitPreparedFrame).
    const char* crosshairLogName;
    AuthoredReticleRefreshPolicy crosshairRefresh;
    bool crosshairAnimatesWhenConfigured; // BoundedAnimation while crosshair_animation_frames != 0
    bool crosshairKeyPerDrawnWidget;      // a key change is not a new weapon's reticle

    // Head-held D-pad gesture (MergeVrPad).
    bool dpadLeftHandBesideHead;    // physical left hand beside the LEFT of the head
    bool dpadClickIsGraphicsSwitch; // stick click = bounded Back = Classic/Anniversary switch
    const char* gestureLogName;
};

inline constexpr TitleTraits kTitleTraits[] = {
    {GameTitle::Halo3, "Halo 3", AuthoredReticleRefreshPolicy::IdentityAndColorState, true, false, false, false,
        "Halo 3"},
    {GameTitle::Halo3ODST, "ODST", AuthoredReticleRefreshPolicy::IdentityImmediate, false, false, false, false, "ODST"},
    {GameTitle::HaloReach, "Reach", AuthoredReticleRefreshPolicy::BoundedAnimation, false, true, false, false, "Reach"},
    {GameTitle::Halo4, "Halo 4", AuthoredReticleRefreshPolicy::BoundedAnimation, false, false, false, false, "Halo 4"},
    {GameTitle::HaloCE, "CE", AuthoredReticleRefreshPolicy::BoundedAnimation, false, false, true, true, "Halo CE"},
    {GameTitle::Halo2, "Halo 2", AuthoredReticleRefreshPolicy::BoundedAnimation, false, false, false, true, "Halo 2"},
};

constexpr const TitleTraits* FindTitleTraits(GameTitle title)
{
    for (const TitleTraits& traits : kTitleTraits)
        if (traits.title == title)
            return &traits;
    return nullptr;
}
