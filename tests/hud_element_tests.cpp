// Per-element HUD placement math (Reach): which HUD draw classes map to which elements, and how
// each element's anchor is pulled toward the view centre. Tests hud_element_logic.h (header-only);
// built as halomccvr_hud_element_tests. Pure logic: no game process.
#include "../src/common/hud_element_logic.h"
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <string>

static int g_failures = 0;
static void Check(bool ok, const char* what)
{
    if (!ok)
    {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}
static bool Near(float a, float b)
{
    return std::fabs(a - b) < 1e-4f;
}

int main()
{
    using namespace hud_element;
    // Class mapping: only the five elements move.
    Check(ElementForReachClass(1) == Weapon && ElementForReachClass(3) == Shield &&
              ElementForReachClass(4) == Grenades && ElementForReachClass(5) == Messages &&
              ElementForReachClass(6) == Motion,
        "five classes map to elements");
    for (int c : {-1, 0, 2, 7, 8, 9})
        Check(ElementForReachClass(c) == -1, "crosshair/titles/parent/unknown stay native");
    Check(IsProjectedReachAnchor(15) && IsProjectedReachAnchor(21) && IsProjectedReachAnchor(24) &&
              !IsProjectedReachAnchor(20) && !IsProjectedReachAnchor(7),
        "projected anchors");

    const float w = 1280, h = 720;
    float dx = 0, dy = 0;
    // Corners pull toward the centre by the fraction.
    Check(Translation({0.25f, 0, 0}, 1, false, w, h, dx, dy) && Near(dx, 160) && Near(dy, 90),
        "top-left pulls right/down");
    Check(Translation({0.25f, 0, 0}, 7, false, w, h, dx, dy) && Near(dx, -160) && Near(dy, -90),
        "bottom-right pulls left/up");
    Check(Translation({0.25f, 0, 0}, 2, false, w, h, dx, dy) && Near(dx, 0) && Near(dy, 90),
        "top-center pulls down only");
    Check(!Translation({0.25f, 0, 0}, 4, false, w, h, dx, dy) && dx == 0 && dy == 0, "centre anchor does not move");
    // Same anchor -> same translation for every widget (no squash).
    float ax = 0, ay = 0, bx = 0, by = 0;
    Translation({0.3f, 5, 5}, 3, false, w, h, ax, ay);
    Translation({0.3f, 5, 5}, 3, false, w, h, bx, by);
    Check(ax == bx && ay == by, "deterministic per anchor");
    // Manual fine-tune: +x right, +y up (HUD Y grows downward).
    Check(Translation({0, 10, 20}, 1, false, w, h, dx, dy) && Near(dx, 10) && Near(dy, -20), "manual offsets");
    // Unknown anchor or size: manual only.
    Check(
        Translation({0.5f, 7, 0}, 0, false, w, h, dx, dy) && Near(dx, 7) && Near(dy, 0), "unknown anchor: manual only");
    Check(
        Translation({0.5f, 0, 3}, 1, false, 0, 0, dx, dy) && Near(dx, 0) && Near(dy, -3), "unknown size: manual only");
    Check(!Translation({0.5f, 0, 0}, 0, false, w, h, dx, dy), "nothing to do -> false");
    // Projected anchors never move.
    Check(!Translation({0.5f, 50, 50}, 1, true, w, h, dx, dy) && dx == 0 && dy == 0, "projected chain untouched");
    // Clamps and bad values.
    Check(Translation({5.0f, 0, 0}, 1, false, w, h, dx, dy) && Near(dx, kPullMax * 640), "pull clamped");
    Check(Translation({0, 1000, -1000}, 1, false, w, h, dx, dy) && Near(dx, kOffsetMax) && Near(dy, -kOffsetMin),
        "offsets clamped");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Check(!Translation({nan, 0, 0}, 1, false, w, h, dx, dy) && !Translation({0, nan, 0}, 1, false, w, h, dx, dy),
        "non-finite settings refused");
    Check(Translation({0.25f, 0, 0}, 1, false, nan, h, dx, dy) == false, "non-finite size: nothing");
    // Halo 3 / ODST proven screen anchors, parent, and slots.
    for (int a : {0, 1, 2, 3, 5, 23})
        Check(IsHalo3ScreenAnchor(a), "proven H3/ODST screen anchor");
    Check(!IsHalo3ScreenAnchor(kHalo3CentreAnchor) && kHalo3CentreAnchor == 13,
        "centre anchor (hit markers, prompts, reticle) never gets a slot");
    for (int a : {4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 22, 24, 25, 26, 27, 28, 38})
        Check(!IsHalo3ScreenAnchor(a), "unproven/object/parent anchor stays stock");
    Check(kHalo3ParentAnchor == 12 && kHalo3BottomRightAnchor == 2, "parent and bottom-right values");
    Check(SlotForPoint(0, 0, w, h) == TopLeft && SlotForPoint(w * 0.5f, 0, w, h) == TopCenter &&
              SlotForPoint(w, 0, w, h) == TopRight && SlotForPoint(0, h, w, h) == BottomLeft &&
              SlotForPoint(w * 0.5f, h, w, h) == BottomCenter && SlotForPoint(w, h, w, h) == BottomRight &&
              SlotForPoint(w * 0.5f, h * 0.5f, w, h) == BottomCenter,
        "anchor origins map to slots");
    Check(SlotForPoint(0, 0, 0, h) == -1 && SlotForPoint(std::numeric_limits<float>::quiet_NaN(), 0, w, h) == -1,
        "no slot without a size or a finite point");
    // Explicit anchor point: same math as the Reach corner mapping.
    float px = 0, py = 0, rx = 0, ry = 0;
    Check(TranslationFromPoint({0.25f, 3, 4}, true, w, h, false, w, h, px, py) &&
              Translation({0.25f, 3, 4}, 7, false, w, h, rx, ry) && px == rx && py == ry,
        "point form equals Reach anchor form");
    Check(TranslationFromPoint({0.25f, 3, 4}, false, w, h, false, w, h, px, py) && Near(px, 3) && Near(py, -4),
        "no anchor point: manual only");
    Check(!TranslationFromPoint({0.25f, 3, 4}, true, 0, 0, true, w, h, px, py), "projected/skipped chain untouched");

    // Per-game rows: separate prefixes, entries and storage.
    Check(std::string(kSetPrefix[SetReach]) == "reach_hud_" && std::string(kSetPrefix[SetHalo2]) == "halo2_hud_" &&
              std::string(kSetPrefix[SetHaloCE]) == "ce_hud_" && std::string(kSetPrefix[SetHalo3]) == "halo3_hud_" &&
              std::string(kSetPrefix[SetOdst]) == "odst_hud_",
        "one key prefix per game");
    Check(SetEntryCount(SetReach) == Count && SetEntryCount(SetHalo2) == Count && SetEntryCount(SetHaloCE) == Count &&
              SetEntryCount(SetHalo3) == SlotCount && SetEntryCount(SetOdst) == SlotCount,
        "entries per game");
    Check(std::string(SetEntryKey(SetReach, Weapon)) == "weapon" &&
              std::string(SetEntryKey(SetHalo3, TopLeft)) == "topleft",
        "entry keys");
    PlacementRow rows[SetCount]{};
    rows[SetReach].x[Weapon] = 50;
    Check(rows[SetHalo2].x[Weapon] == 0 && rows[SetHaloCE].x[Weapon] == 0 && rows[SetReach].At(Weapon).x == 50,
        "tuning one game never moves another");
    Check(rows[SetOdst].At(5).pull == kDefaultPull && rows[SetOdst].At(6).pull == 0 && rows[SetOdst].At(-1).x == 0,
        "row defaults and bounds");

    // Defaults.
    Placement d{};
    Check(d.pull == kDefaultPull && d.x == 0 && d.y == 0, "default placement");

    if (g_failures)
    {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("hud_element_tests: all passed\n");
    return 0;
}
