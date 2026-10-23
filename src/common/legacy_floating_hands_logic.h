#pragma once
// Halo 3 / ODST floating hands: which body nodes collapse so only hands and gun show.
// Used by dll/core/game.cpp and tests/legacy_floating_hands_tests.cpp.
//
// Halo 3 / ODST floating hands: hide the arms but
// keep both hands and the held gun. Arm joints COLLAPSE ONTO THEIR OWN WRIST
// (as Reach, CE, Halo 4 and the earlier CE VR mod do), so a vertex blended between a
// forearm and the wrist lands on the wrist instead of stretching toward a
// shrunk elbow ("tether" triangles). Other body nodes shrink where they are.
// camera_control is kept: H3 weapon vertices share its weight with r_hand and
// root, and the arms-visible palette (headset-accepted gun) leaves it intact.
//
// Pure logic: no globals.
#include <cstdint>

namespace legacy_floating_hands
{
    inline constexpr float kHiddenScale = 0.0001f;

    enum class Action : uint8_t
    {
        Keep,
        CollapseAtRightWrist,
        CollapseAtLeftWrist,
        HideInPlace
    };

    struct Layout
    {
        int count = 0;
        uint64_t rightKeep = 0, leftKeep = 0; // wrist descendants (hand + fingers [+ gun])
        int rightWrist = -1, rightElbow = -1, rightShoulder = -1;
        int leftWrist = -1, leftElbow = -1, leftShoulder = -1;
        int cameraControl = -1;
    };

    inline bool ValidNode(const Layout& l, int i) noexcept
    {
        return i >= 0 && i < l.count && i < 64;
    }

    // collapseAtWrist=false is the pre-14a behaviour exactly: every node outside the
    // two wrist subtrees (camera_control included) shrinks where it stands.
    inline Action For(const Layout& l, int i, bool collapseAtWrist = true) noexcept
    {
        if (!ValidNode(l, i))
            return Action::Keep;
        const uint64_t bit = uint64_t{1} << i;
        if ((l.rightKeep | l.leftKeep) & bit)
            return Action::Keep;
        if (!collapseAtWrist)
            return Action::HideInPlace;
        if (i == l.cameraControl)
            return Action::Keep;
        if ((i == l.rightElbow || i == l.rightShoulder) && ValidNode(l, l.rightWrist) &&
            (l.rightKeep & (uint64_t{1} << l.rightWrist)))
            return Action::CollapseAtRightWrist;
        if ((i == l.leftElbow || i == l.leftShoulder) && ValidNode(l, l.leftWrist) &&
            (l.leftKeep & (uint64_t{1} << l.leftWrist)))
            return Action::CollapseAtLeftWrist;
        return Action::HideInPlace;
    }

    // Bone: any palette record with a `scale` member (BoneMatrix). The wrists are
    // kept, so collapsing reads the already-solved wrist record.
    template <class Bone>
    inline void Apply(const Layout& l, Bone* palette, bool collapseAtWrist = true) noexcept
    {
        if (!palette || l.count <= 0 || l.count > 64)
            return;
        for (int i = 0; i < l.count; ++i)
        {
            switch (For(l, i, collapseAtWrist))
            {
            case Action::CollapseAtRightWrist:
                palette[i] = palette[l.rightWrist];
                palette[i].scale = kHiddenScale;
                break;
            case Action::CollapseAtLeftWrist:
                palette[i] = palette[l.leftWrist];
                palette[i].scale = kHiddenScale;
                break;
            case Action::HideInPlace:
                palette[i].scale = kHiddenScale;
                break;
            case Action::Keep:
                break;
            }
        }
    }
} // namespace legacy_floating_hands
