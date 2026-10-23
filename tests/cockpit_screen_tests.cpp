// Golden tests for the enclosed-cockpit screen logic (classes, hull camera, lens, placement, anchor).
// Built as halomccvr_cockpit_screen_tests and run by ctest; header-only src/common/cockpit_screen_logic.h.
// Pure logic: no game process, no headset, no Windows calls.
#include "../src/common/cockpit_screen_logic.h"
#include <cstdio>
#include <limits>

using namespace cockpit_screen;
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
    bool Near(float a, float b, float e = 1e-4f)
    {
        return std::fabs(a - b) <= e;
    }
}

int main()
{
    // Which Reach seats get a screen: enclosed driver/pilot seats only.
    Check(ReachClassOf(ReachVehicleId::Scorpion, 0) == Class::Tank &&
              ReachClassOf(ReachVehicleId::Wraith, 0) == Class::Tank,
        "Scorpion and Wraith drivers are tanks");
    Check(ReachClassOf(ReachVehicleId::Banshee, 0) == Class::Flyer &&
              ReachClassOf(ReachVehicleId::SpaceBanshee, 0) == Class::Flyer &&
              ReachClassOf(ReachVehicleId::Seraph, 0) == Class::Flyer,
        "Banshee, space Banshee and Seraph pilots are flyers");
    Check(ReachClassOf(ReachVehicleId::Scorpion, 1) == Class::None &&
              ReachClassOf(ReachVehicleId::Warthog, 0) == Class::None &&
              ReachClassOf(ReachVehicleId::WraithGunner, 0) == Class::None &&
              ReachClassOf(ReachVehicleId::Unknown, 0) == Class::None,
        "passengers, open and unknown vehicles get no screen");
    Check(LegacyClassOf(1, 0, false) == Class::Tank && LegacyClassOf(5, 0, false) == Class::Tank &&
              LegacyClassOf(7, 0, false) == Class::Flyer && LegacyClassOf(2, 0, false) == Class::None &&
              LegacyClassOf(1, 1, false) == Class::None && LegacyClassOf(5, 0, true) == Class::None &&
              LegacyClassOf(0, 0, false) == Class::None,
        "Halo 3 / ODST: Scorpion and Wraith drivers are tanks, the Banshee pilot a flyer; passengers, turrets and open vehicles none");
    Check(Near(kWorldUnitsPerMeter * 3.048f, 1.0f), "one world unit is ten feet");
    Check(Halo2ClassOf(0x1E6E302886CF3D7Bull, 0) == Class::Tank &&
              Halo2ClassOf(0x55D8FE797FA258C7ull, 0) == Class::Tank &&
              Halo2ClassOf(0x500C52F8DDCD350Full, 0) == Class::Flyer &&
              Halo2ClassOf(0x500C52F8DDCD350Full, 1) == Class::None &&
              Halo2ClassOf(0x0123456789ABCDEFull, 0) == Class::None,
        "Halo 2: Scorpion and Wraith drivers are tanks, the Banshee pilot a flyer; others none");
    Check(Wanted(true, Class::Tank, true, true, false) && !Wanted(false, Class::Tank, true, true, false) &&
              !Wanted(true, Class::None, true, true, false) && !Wanted(true, Class::Tank, false, true, false) &&
              !Wanted(true, Class::Tank, true, false, false) && !Wanted(true, Class::Tank, true, true, true),
        "the screen needs the switch, an enclosed class, a proven hull and no cutscene");

    // Hull camera: ahead along the hull forward, raised, level up vector.
    float p[3], f[3], u[3];
    const float base[3]{10, 20, 5}, hull[3]{0, 2, 0}; // facing +Y, any length
    Check(CameraPose(base, hull, 4.0f, 1.2f, p, f, u) && Near(p[0], 10) && Near(p[1], 24) && Near(p[2], 6.2f) &&
              Near(f[1], 1) && Near(u[2], 1),
        "camera sits ahead of and above the seat, looking along the hull");
    const float pitched[3]{0, 0.9f, 0.3f};
    Check(CameraPose(base, pitched, 0, 0, p, f, u) && Near(f[0] * u[0] + f[1] * u[1] + f[2] * u[2], 0) && u[2] > 0.9f,
        "a pitched hull keeps an orthogonal, upright up vector");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float vertical[3]{0, 0, 1}, zero[3]{0, 0, 0}, bad[3]{nan, 0, 0};
    Check(!CameraPose(base, vertical, 1, 1, p, f, u) && !CameraPose(base, zero, 1, 1, p, f, u) &&
              !CameraPose(bad, hull, 1, 1, p, f, u) && !CameraPose(base, hull, nan, 1, p, f, u),
        "vertical, zero and non-finite inputs are refused (stock fallback)");

    // Lens: the 4:3 window shows the configured horizontal field of view.
    const Lens wide = LensFor(90.0f, 2912.0f / 2100.0f);
    Check(Near(wide.vertical * 4.0f / 3.0f, 1.0f, 1e-3f) && Near(wide.horizontal, wide.vertical * 2912.0f / 2100.0f),
        "wider-than-4:3 source: the window keeps the full height at the asked field of view");
    const Lens tall = LensFor(90.0f, 1.0f);
    Check(Near(tall.horizontal, 1.0f, 1e-3f), "narrower source: the window keeps the full width");
    Check(Near(LensFor(nan, 1.5f).horizontal, LensFor(90.0f, 1.5f).horizontal) &&
              LensFor(200.0f, 1.5f).vertical == LensFor(120.0f, 1.5f).vertical,
        "bad or extreme field of view is sanitised");

    // Placement: fixed in the cockpit, straight ahead of the captured eye.
    const float eye[3]{1, 1.2f, 2};
    const Quad ahead = Place(eye, 0.0f, 0.6f, 0.8f, -0.05f);
    Check(ahead.valid && Near(ahead.position[0], 1) && Near(ahead.position[1], 1.15f) &&
              Near(ahead.position[2], 1.2f) && Near(ahead.width, 0.6f) && Near(ahead.height, 0.45f) &&
              Near(ahead.orientation[3], 1),
        "yaw 0: 0.8 m straight ahead, 5 cm below the eye, 4:3");
    const Quad left = Place(eye, 1.5707963f, 0.6f, 0.8f, 0.0f);
    Check(Near(left.position[0], 0.2f) && Near(left.position[2], 2) && Near(left.orientation[1], std::sqrt(0.5f)),
        "yaw 90 deg: the display follows the way the player faced at entry");
    Check(!Place(bad, 0, 0.6f, 0.8f, 0).valid && Place(eye, 0, 99.0f, 99.0f, 99.0f).width == 1.5f,
        "bad eye is refused; sizes clamp");

    // Anchor: captured once, re-captured on a position recenter.
    Anchor a;
    const float head1[3]{0, 1.6f, 0}, head2[3]{0, 1.1f, 0};
    Check(a.Update(head1, 0.5f, 1) && a.Valid() && Near(a.Eye()[1], 1.6f) && Near(a.Yaw(), 0.5f),
        "first update captures");
    Check(!a.Update(head2, 1.0f, 1) && Near(a.Eye()[1], 1.6f), "same recenter epoch keeps the anchor");
    Check(a.Update(head2, 1.0f, 2) && Near(a.Eye()[1], 1.1f) && Near(a.Yaw(), 1.0f), "a recenter re-captures it");
    a.Reset();
    Check(!a.Valid() && !a.Update(bad, 0, 3), "reset forgets; a bad head captures nothing");

    std::printf("cockpit screen: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
