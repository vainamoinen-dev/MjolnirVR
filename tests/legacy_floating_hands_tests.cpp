// Golden: Halo 3 / ODST floating-hands node maps (arm joints collapse onto their own wrist).
// Tests legacy_floating_hands_logic.h with odst_bringup_logic.h; header-only, built as
// halomccvr_legacy_floating_hands_tests. Pure logic: no game process.
#include "../src/common/legacy_floating_hands_logic.h"
#include "../src/common/titles/odst/odst_bringup_logic.h"
#include <cstdio>
#include <string_view>

using namespace legacy_floating_hands;
namespace
{
    unsigned checks{}, failures{};
    void Check(bool ok, const char* message)
    {
        ++checks;
        if (!ok)
        {
            ++failures;
            std::printf("FAIL: %s\n", message);
        }
    }
    struct Bone
    {
        float scale;
        float rotation[9];
        float translation[3];
    };
}

int main()
{
    {
        // ODST: pinned H3ODSTEK layout, 45 nodes (body 0..36, weapon 37..43, camera_control 44).
        OdstFpSkeletonLayout odst{};
        Check(ComputeOdstFpSkeletonLayout(45, odst), "ODST 45-node layout");
        Layout l{45, odst.rightHandAndWeaponDescendants, odst.leftHandDescendants, odst.rightWrist, odst.rightElbow,
            odst.rightShoulder, odst.leftWrist, odst.leftElbow, odst.leftShoulder, odst.cameraControl};
        char map[46]{};
        for (int i = 0; i < 45; ++i)
        {
            const Action a = For(l, i);
            map[i] = a == Action::Keep                   ? 'K'
                     : a == Action::CollapseAtRightWrist ? 'R'
                     : a == Action::CollapseAtLeftWrist  ? 'L'
                                                         : 'h';
        }
        // 0 root hidden; 1 L shoulder, 2 R shoulder, 3 L elbow, 4 R elbow onto their wrists;
        // hands/fingers 5..36, weapon 37..43 and camera_control 44 kept.
        Check(std::string_view(map) == "hLRLRKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK", "ODST node map pinned");

        Bone palette[45]{};
        for (int i = 0; i < 45; ++i)
        {
            palette[i].scale = 1;
            palette[i].translation[0] = float(i);
        }
        Apply(l, palette);
        Check(palette[4].translation[0] == 6 && palette[2].translation[0] == 6 && palette[4].scale == kHiddenScale,
            "right elbow and shoulder collapse onto the right wrist");
        Check(palette[3].translation[0] == 5 && palette[1].translation[0] == 5 && palette[1].scale == kHiddenScale,
            "left elbow and shoulder collapse onto the left wrist");
        Check(palette[0].translation[0] == 0 && palette[0].scale == kHiddenScale, "root shrinks in place");
        Check(palette[6].scale == 1 && palette[40].scale == 1 && palette[44].scale == 1 &&
                  palette[44].translation[0] == 44,
            "wrist, weapon and camera_control untouched");
    }
    {
        // Halo 3 (runtime lookup): a representative AR rig. r_hand 9 with fingers/gun 10..14,
        // l_hand 6 with 7..8, elbows 5/4, shoulders 3/2, camera_control 1, root 0, spare 15.
        Layout l{16, 0x7E00, 0x1C0, 9, 5, 3, 6, 4, 2, 1};
        Check(For(l, 5) == Action::CollapseAtRightWrist && For(l, 3) == Action::CollapseAtRightWrist &&
                  For(l, 4) == Action::CollapseAtLeftWrist && For(l, 2) == Action::CollapseAtLeftWrist,
            "H3 arm joints collapse onto their own wrist");
        Check(For(l, 0) == Action::HideInPlace && For(l, 15) == Action::HideInPlace && For(l, 1) == Action::Keep &&
                  For(l, 12) == Action::Keep && For(l, 7) == Action::Keep,
            "H3 other body nodes hide; hands, gun, camera kept");
        Layout noLeft = l;
        noLeft.leftWrist = -1;
        noLeft.leftKeep = 0;
        Check(For(noLeft, 4) == Action::HideInPlace,
            "no left wrist: its arm shrinks in place, never collapses to garbage");
        Layout bad = l;
        bad.count = 80;
        Bone palette[16]{};
        palette[5].scale = 1;
        Apply(bad, palette);
        Check(palette[5].scale == 1, "an out-of-range palette is left alone");
    }
    {
        // collapseAtWrist=false is the old behaviour: everything outside the wrist subtrees
        // (camera_control too) shrinks in place, nothing is copied onto a wrist.
        Layout l{16, 0x7E00, 0x1C0, 9, 5, 3, 6, 4, 2, 1};
        Bone palette[16]{};
        for (int i = 0; i < 16; ++i)
        {
            palette[i].scale = 1;
            palette[i].translation[0] = float(i);
        }
        Apply(l, palette, false);
        bool legacy = true;
        for (int i = 0; i < 16; ++i)
        {
            const bool kept = (((l.rightKeep | l.leftKeep) >> i) & 1u) != 0;
            legacy =
                legacy && palette[i].translation[0] == float(i) && palette[i].scale == (kept ? 1.0f : kHiddenScale);
        }
        Check(legacy, "collapse off: every non-wrist-subtree node (camera_control too) shrinks in place");
    }
    std::printf("legacy floating hands: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
