#pragma once
// Per-element HUD placement (Reach first). Pure math: no globals, no logging.
// Used by common/config.h, titles/h2/halo2_hud_shader_logic.h and the HUD element tests.
//
// A HUD element (weapon/ammo, shield, grenades, messages, motion sensor) is a
// group of widgets that share an anchor (a screen corner or edge midpoint).
// Moving each widget's own position toward the centre would squash the
// element, so every widget of an element gets the SAME translation:
//   pull * (screen centre - anchor point) + (x, y)
// pull 0 keeps the element where the game put it; pull 1 would bring its
// anchor point to the centre. x/y are a manual fine-tune in HUD pixels
// (+x right, +y up, like hud_vertical_offset; HUD pixel Y grows downward).
// Projected (world-tracking) anchors are never moved; an unknown anchor or an
// unknown screen size gets only the manual x/y.
#include <cmath>
#include <cstdint>

namespace hud_element
{
    enum Element : int
    {
        Weapon = 0,
        Shield,
        Grenades,
        Messages,
        Motion,
        Count
    };

    inline constexpr const char* kElementKeys[Count]{"weapon", "shield", "grenades", "messages", "motion"};
    inline constexpr const char* kElementLabels[Count]{
        "Weapon and ammo", "Shield and health", "Grenades", "Messages and objectives", "Motion sensor"};

    inline constexpr float kPullMin = 0.0f;
    inline constexpr float kPullMax = 0.6f;
    inline constexpr float kOffsetMin = -300.0f;
    inline constexpr float kOffsetMax = 300.0f;
    // Default pull for every element: brings a corner element a quarter of the way
    // in. the owner tunes per element in the headset (comfort target ~15-20 deg).
    inline constexpr float kDefaultPull = 0.25f;

    struct Placement
    {
        float pull = kDefaultPull, x = 0, y = 0;
    };

    // A learned virtual HUD size is used only if it is a plausible screen: both
    // sides finite, above 1 px and at most 4K (4096).
    inline constexpr float kVirtualSizeMax = 4096.0f;
    inline bool PlausibleVirtualSize(float width, float height) noexcept
    {
        return std::isfinite(width) && std::isfinite(height) && width > 1.0f && height > 1.0f &&
               width <= kVirtualSizeMax && height <= kVirtualSizeMax;
    }

    // Reach CHUD scripting class (owning collection) -> element. 2 crosshair,
    // 7 chapter title, 8 cinematics, 0 parent and anything else stay native.
    constexpr int ElementForReachClass(int chudClass) noexcept
    {
        switch (chudClass)
        {
        case 1:
            return Weapon;
        case 3:
            return Shield;
        case 4:
            return Grenades;
        case 5:
            return Messages;
        case 6:
            return Motion;
        default:
            return -1;
        }
    }

    // Reach anchor types that place a widget on a projected world position.
    constexpr bool IsProjectedReachAnchor(int anchor) noexcept
    {
        return anchor == 15 || (anchor >= 21 && anchor <= 24);
    }

    // Screen point of a Reach screen anchor (1..7) in a width x height HUD.
    inline bool ReachAnchorPoint(int anchor, float width, float height, float& x, float& y) noexcept
    {
        if (!(width > 0) || !(height > 0) || !std::isfinite(width) || !std::isfinite(height))
            return false;
        switch (anchor)
        {
        case 1:
            x = 0;
            y = 0;
            return true; // top-left
        case 2:
            x = width * 0.5f;
            y = 0;
            return true; // top-center
        case 3:
            x = width;
            y = 0;
            return true; // top-right
        case 4:
            x = width * 0.5f;
            y = height * 0.5f;
            return true; // center
        case 5:
            x = 0;
            y = height;
            return true; // bottom-left
        case 6:
            x = width * 0.5f;
            y = height;
            return true; // bottom-center
        case 7:
            x = width;
            y = height;
            return true; // bottom-right
        default:
            return false;
        }
    }

    inline float Clamp(float v, float lo, float hi) noexcept
    {
        return v < lo ? lo : v > hi ? hi : v;
    }

    // Translation for one widget whose anchor point (ax, ay) is known
    // (haveAnchor) in a width x height HUD. False: leave the widget exactly where
    // the game put it.
    inline bool TranslationFromPoint(const Placement& placement, bool haveAnchor, float ax, float ay, bool projected,
        float width, float height, float& dx, float& dy) noexcept
    {
        dx = dy = 0;
        if (projected || !std::isfinite(placement.pull) || !std::isfinite(placement.x) || !std::isfinite(placement.y))
            return false;
        const float pull = Clamp(placement.pull, kPullMin, kPullMax);
        if (pull > 0 && haveAnchor && std::isfinite(ax) && std::isfinite(ay) && width > 0 && height > 0 &&
            std::isfinite(width) && std::isfinite(height))
        {
            dx = pull * (width * 0.5f - ax);
            dy = pull * (height * 0.5f - ay);
        }
        dx += Clamp(placement.x, kOffsetMin, kOffsetMax);
        dy -= Clamp(placement.y, kOffsetMin, kOffsetMax);
        return std::isfinite(dx) && std::isfinite(dy) && (dx != 0 || dy != 0);
    }

    // Reach: `anchor` is the screen anchor its chain resolved to (0 = none found).
    inline bool Translation(const Placement& placement, int anchor, bool projected, float width, float height,
        float& dx, float& dy) noexcept
    {
        float ax = 0, ay = 0;
        const bool have = ReachAnchorPoint(anchor, width, height, ax, ay);
        return TranslationFromPoint(placement, have, ax, ay, projected, width, height, dx, dy);
    }

    // ---- Halo 3 and ODST (chud_compute_anchor_basis, H3 +0x2EE234, ODST +0x329824).
    // They expose no per-widget CHUD class, so placement is per screen position.
    // Anchor values proven from both retail switches (identical for these cases):
    // 0 top-left (0,0), 1 top-right (W,0),
    // 2 bottom-right (W,H), 3 bottom-left (0,H), 5 top-centre (W/2,0),
    // 13 centre (W/2,H/2), 23 bottom-centre (W/2,H); 12 is the parent anchor
    // (recurses). Every other value (projected/object anchors, per-user and
    // scaled-edge anchors) is left exactly where the game puts it. The centre
    // anchor 13 is proven but deliberately gets NO slot: hit markers, prompts and
    // possibly the reticle hang off it, and a manual offset must never move them.
    // The anchor point is the origin the game wrote to basis +0x28/+0x2C.
    constexpr bool IsHalo3ScreenAnchor(int anchor) noexcept
    {
        return anchor == 0 || anchor == 1 || anchor == 2 || anchor == 3 || anchor == 5 || anchor == 23;
    }
    inline constexpr int kHalo3CentreAnchor = 13;
    inline constexpr int kHalo3ParentAnchor = 12;
    inline constexpr int kHalo3BottomRightAnchor = 2;

    enum Slot : int
    {
        TopLeft = 0,
        TopCenter,
        TopRight,
        BottomLeft,
        BottomCenter,
        BottomRight,
        SlotCount
    };
    inline constexpr const char* kSlotKeys[SlotCount]{
        "topleft", "topcenter", "topright", "bottomleft", "bottomcenter", "bottomright"};
    inline constexpr const char* kSlotLabels[SlotCount]{
        "Top left", "Top centre", "Top right", "Bottom left", "Bottom centre", "Bottom right"};

    // Screen position slot of an anchor point: left/centre/right thirds, top or
    // bottom half (the centre anchor counts as bottom centre; its pull is zero).
    inline int SlotForPoint(float x, float y, float width, float height) noexcept
    {
        if (!(width > 0) || !(height > 0) || !std::isfinite(x) || !std::isfinite(y))
            return -1;
        const int column = x < width / 3 ? 0 : x > width * 2 / 3 ? 2 : 1;
        return (y < height * 0.5f ? 0 : 3) + column;
    }

    // ---- Per-game placement settings. Each game owns its own row, so tuning one
    // game never moves another (their units differ too). Reach, Halo 2 and CE are
    // indexed by Element; Halo 3 and ODST by Slot.
    enum PlacementSet : int
    {
        SetReach = 0,
        SetHalo2,
        SetHaloCE,
        SetHalo3,
        SetOdst,
        SetCount
    };
    inline constexpr int kPlacementMax = SlotCount; // >= Count
    static_assert(Count <= kPlacementMax, "placement row too small");
    inline constexpr const char* kSetPrefix[SetCount]{"reach_hud_", "halo2_hud_", "ce_hud_", "halo3_hud_", "odst_hud_"};
    constexpr bool SetUsesSlots(int set) noexcept
    {
        return set == SetHalo3 || set == SetOdst;
    }
    constexpr int SetEntryCount(int set) noexcept
    {
        return SetUsesSlots(set) ? SlotCount : Count;
    }
    inline const char* SetEntryKey(int set, int index) noexcept
    {
        return SetUsesSlots(set) ? kSlotKeys[index] : kElementKeys[index];
    }
    inline const char* SetEntryLabel(int set, int index) noexcept
    {
        return SetUsesSlots(set) ? kSlotLabels[index] : kElementLabels[index];
    }

    struct PlacementRow
    {
        float pull[kPlacementMax]{kDefaultPull, kDefaultPull, kDefaultPull, kDefaultPull, kDefaultPull, kDefaultPull};
        float x[kPlacementMax]{};
        float y[kPlacementMax]{};
        Placement At(int index) const noexcept
        {
            if (index < 0 || index >= kPlacementMax)
                return Placement{0, 0, 0};
            return {pull[index], x[index], y[index]};
        }
    };
} // namespace hud_element
