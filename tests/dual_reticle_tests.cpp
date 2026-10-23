// Tests for the dual-wield reticle rules and the barrel-ray projection (src/common/dual_reticle_logic.h).
// Built as dual_reticle_tests and run by ctest. Header-only logic: no game process, no headset.
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>
#include "../src/common/dual_reticle_logic.h"
static unsigned checks;
static void Check(bool pass, const char* why)
{
    ++checks;
    if (!pass)
    {
        std::fprintf(stderr, "FAIL: %s\n", why);
        std::exit(1);
    }
}
static bool Near(float a, float b)
{
    return std::abs(a - b) < .0001f;
}
int main()
{
    for (auto title : {GameTitle::Halo2, GameTitle::Halo3})
    {
        Check(dual_reticle::Eligible(title, true, true, true, true, false, false), "supported dual pair");
        Check(!dual_reticle::Eligible(title, false, true, true, true, false, false), "single gun retains ordinary art");
        Check(!dual_reticle::Eligible(title, true, true, false, true, false, false), "secondary tracking loss");
        Check(!dual_reticle::Eligible(title, true, true, true, false, false, false), "crosshair disabled");
        Check(!dual_reticle::Eligible(title, true, true, true, true, true, false), "HUD hidden");
        Check(!dual_reticle::Eligible(title, true, true, true, true, false, true), "theatre excludes gun reticles");
    }
    Check(!dual_reticle::Eligible(GameTitle::HaloReach, true, true, true, true, false, false),
        "no unsupported native dual authority");
    // Halo 4, Reach and ODST reticles ride the barrel receipt; none gains a dual reticle.
    for (auto title :
        {GameTitle::Halo2, GameTitle::Halo3, GameTitle::Halo4, GameTitle::HaloReach, GameTitle::Halo3ODST})
        Check(dual_reticle::BarrelRayTitle(title), "barrel-ray reticle title");
    Check(!dual_reticle::BarrelRayTitle(GameTitle::HaloCE), "CE reticle stays on the hand ray");
    Check(!dual_reticle::Supported(GameTitle::Halo4) && !dual_reticle::Supported(GameTitle::HaloReach) &&
              !dual_reticle::Supported(GameTitle::Halo3ODST),
        "Halo 4, Reach and ODST have no dual-weapon reticle");
    Check(dual_reticle::PhysicalHand(0, false) == 1 && dual_reticle::PhysicalHand(1, false) == 0,
        "ordinary physical roles");
    Check(dual_reticle::PhysicalHand(0, true) == 0 && dual_reticle::PhysicalHand(1, true) == 1, "left handed roles");
    const auto trim = dual_reticle::Mirror(11, 22, 33);
    Check(trim.yaw == -11 && trim.pitch == 22 && trim.roll == -33, "secondary mount trim reflection");
    contact_melee::TrackingToWorld transform{};
    transform.axis[0] = {0, -1, 0};
    transform.axis[1] = {0, 0, 1};
    transform.axis[2] = {-1, 0, 0};
    transform.origin = {120, 80, -30};
    transform.unitsPerMetre = 3.048f;
    weapon_muzzle::Receipt source{GameTitle::Halo2, 7, 0x120001, 0x250005, 99, 11, 25, 500, 9000000, 1, 0, true, {}};
    const auto world = transform.World({-.3f, 1.5f, -.8f});
    source.ray.position[0] = world.x;
    source.ray.position[1] = world.y;
    source.ray.position[2] = world.z;
    source.ray.direction[0] = 1;
    source.ray.up[2] = 1;
    dual_reticle::ProjectedRay projected{};
    Check(dual_reticle::Project(source, transform, projected), "committed world muzzle projects");
    Check(Near(projected.tracking.position[0], -.3f) && Near(projected.tracking.position[1], 1.5f) &&
              Near(projected.tracking.position[2], -.8f),
        "world to XR origin including translation and scale");
    Check(Near(projected.tracking.direction[0], 0) && Near(projected.tracking.direction[1], 0) &&
              Near(projected.tracking.direction[2], -1),
        "direction inverse basis excludes translation and scale");
    Check(dual_reticle::Current(projected, GameTitle::Halo2, 7, 11, 25, 9000000, 550, 1, true),
        "exact prepared palette accepted");
    Check(
        !dual_reticle::Current(projected, GameTitle::Halo3, 7, 11, 25, 9000000, 550, 1, true), "cross-title rejected");
    // A frame or two behind the palette keeps the barrel line (no one-frame jump to the hand ray).
    Check(dual_reticle::Recent(projected, GameTitle::Halo2, 7, 11, 500 + dual_reticle::kRecentMs, 1, true),
        "recent ray from the same gun and space is held across a missed serial");
    Check(!dual_reticle::Recent(projected, GameTitle::Halo2, 7, 11, 501 + dual_reticle::kRecentMs, 1, true),
        "held only within the bound");
    Check(!dual_reticle::Recent(projected, GameTitle::Halo2, 8, 11, 550, 1, true), "new generation is not held");
    Check(!dual_reticle::Recent(projected, GameTitle::Halo2, 7, 12, 550, 1, true), "recentred space is not held");
    Check(!dual_reticle::Recent(projected, GameTitle::Halo2, 7, 11, 550, 0, true), "other slot is not held");
    Check(!dual_reticle::Recent(projected, GameTitle::Halo2, 7, 11, 550, 1, false), "handedness switch is not held");
    Check(!dual_reticle::Recent(projected, GameTitle::Halo3, 7, 11, 550, 1, true), "other title is not held");
    Check(!dual_reticle::Current(projected, GameTitle::Halo2, 8, 11, 25, 9000000, 550, 1, true),
        "old generation rejected");
    Check(!dual_reticle::Current(projected, GameTitle::Halo2, 7, 12, 25, 9000000, 550, 1, true), "recenter rejected");
    Check(!dual_reticle::Current(projected, GameTitle::Halo2, 7, 11, 26, 9000000, 550, 1, true),
        "missed palette cannot reuse prior serial");
    Check(!dual_reticle::Current(projected, GameTitle::Halo2, 7, 11, 25, 9000001, 550, 1, true),
        "different predicted time rejected");
    Check(!dual_reticle::Current(projected, GameTitle::Halo2, 7, 11, 25, 9000000, 601, 1, true),
        "expired palette rejected");
    Check(!dual_reticle::Current(projected, GameTitle::Halo2, 7, 11, 25, 9000000, 550, 0, true),
        "primary cannot consume secondary ray");
    Check(!dual_reticle::Current(projected, GameTitle::Halo2, 7, 11, 25, 9000000, 550, 1, false),
        "handedness switch rejected");
    for (auto title : {GameTitle::Halo4, GameTitle::HaloReach, GameTitle::Halo3ODST})
    {
        auto barrel = source;
        barrel.title = title;
        barrel.slot = 0;
        dual_reticle::ProjectedRay own{};
        Check(dual_reticle::Project(barrel, transform, own) &&
                  dual_reticle::Current(own, title, 7, 11, 25, 9000000, 550, 0, true) &&
                  Near(own.tracking.direction[2], -1),
            "Halo 4 / Reach / ODST barrel receipt projects and is current");
    }
    transform.unitsPerMetre = 0;
    Check(!dual_reticle::Project(source, transform, projected), "invalid scale rejected");
    transform.unitsPerMetre = 1;
    source.ray.direction[0] = std::numeric_limits<float>::quiet_NaN();
    Check(!dual_reticle::Project(source, transform, projected), "nonfinite ray rejected");
    std::printf("PASS: %u dual reticle checks\n", checks);
}
