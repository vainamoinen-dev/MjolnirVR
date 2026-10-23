// Tests vehicle_monitors_logic.h: the per-title vehicle monitor camera table and the side and
// forward views it produces. Header-only; built as halomccvr_vehicle_monitors_tests.
// Pure logic: no game process.
#include "../src/common/vehicle_monitors_logic.h"
#include <cstdio>
#include <cstring>

static int failures = 0, checks = 0;
static void Check(bool good, const char* label)
{
    ++checks;
    if (!good)
    {
        ++failures;
        std::printf("FAIL: %s\n", label);
    }
}

int main()
{
    using namespace vehicle_monitors;
    Check(kRowCount == 14, "the 14-row camera table from the design note");
    const CameraRow* ceScorpion = RowFor(Game::HaloCE, "scorpion");
    Check(ceScorpion && ceScorpion->aheadM == 5.3f && ceScorpion->upM == 1.5f && ceScorpion->verified,
        "CE Scorpion row: 5.3 m ahead, 1.5 m up");
    Check(RowFor(Game::Reach, "seraph") && !RowFor(Game::Reach, "seraph")->verified,
        "the Reach Seraph row is unverified");
    Check(RowFor(Game::Reach, "banshee")->sideYawDeg + RowFor(Game::Reach, "banshee")->fovDeg * 0.5f < 90.0f,
        "Reach side views stay inside the head hemisphere its scope pass allows");
    Check(!RowFor(Game::Reach, "warthog") && !RowFor(Game::Reach, nullptr), "unknown vehicles have no row");
    Check(LegacyVehicleName(1, 0, false) && !LegacyVehicleName(1, 1, false) && !LegacyVehicleName(1, 0, true),
        "H3/ODST names only for the driver seat");
    Check(ReachVehicleName(ReachVehicleId::Banshee, 0) && !ReachVehicleName(ReachVehicleId::Warthog, 0),
        "Reach names only enclosed vehicles");
    Check(HaloCEClassOf(kHaloCEScorpionIdentity, 0) == cockpit_screen::Class::Tank &&
              HaloCEClassOf(kHaloCEBansheeIdentity, 0) == cockpit_screen::Class::Flyer &&
              HaloCEClassOf(kHaloCEScorpionIdentity, 1) == cockpit_screen::Class::None &&
              HaloCEClassOf(0xE2E54B251CFFE5EDull, 0) == cockpit_screen::Class::None,
        "CE: Scorpion/Wraith tanks, Banshee flyer, driver seat only, Warthog open");

    // Views: side cameras share the origin and turn +-75 deg; FOV 60.
    const CameraSpec f = SpecFor(ceScorpion, cockpit_screen::Class::Tank, View::Forward);
    const CameraSpec l = SpecFor(ceScorpion, cockpit_screen::Class::Tank, View::Left);
    const CameraSpec r = SpecFor(ceScorpion, cockpit_screen::Class::Tank, View::Right);
    Check(f.yawDeg == 0 && l.yawDeg == 75 && r.yawDeg == -75 && f.fovDeg == 60 && l.aheadM == f.aheadM,
        "forward/left/right yaw and shared origin");
    const CameraSpec none = SpecFor(nullptr, cockpit_screen::Class::Flyer, View::Forward);
    Check(none.aheadM == cockpit_screen::CameraAheadM(cockpit_screen::Class::Flyer), "no row: class values");

    {
        float fw[3]{1, 0, 0};
        const float up[3]{0, 0, 1};
        YawAboutUp(fw, up, 90);
        Check(std::fabs(fw[0]) < 1e-5f && std::fabs(fw[1] - 1) < 1e-5f, "+90 yaw turns forward to the left (+y)");
    }
    {
        const float eye[3]{0, 1.2f, 0};
        const auto c = Place(eye, 0, View::Forward), a = Place(eye, 0, View::Left), b = Place(eye, 0, View::Right);
        Check(c.valid && std::fabs(c.position[0]) < 1e-5f && std::fabs(c.position[2] + 0.60f) < 1e-4f,
            "centre screen 0.60 m straight ahead");
        Check(a.position[0] < -0.3f && b.position[0] > 0.3f && a.orientation[1] > 0 && b.orientation[1] < 0,
            "side screens left/right, each turned to face the eye");
        const float topEdge = c.position[1] + c.height * 0.5f - eye[1];
        Check(std::fabs(c.width - 0.42f) < 1e-5f && std::fabs(c.height - 0.315f) < 1e-4f &&
                  std::fabs(topEdge + std::tan(5 * 0.01745329252f) * 0.60f) < 1e-4f,
            "0.42 m 4:3 screens, top edge 5 deg below the eye line");
    }
    {
        RoundRobin rr;
        const bool all[3]{true, true, true}, noLeft[3]{true, false, true}, none3[3]{};
        Check(rr.Next(all) == 0 && rr.Next(all) == 1 && rr.Next(all) == 2 && rr.Next(all) == 0, "round-robin F, L, R");
        Check(rr.Next(noLeft) == 2 && rr.Next(noLeft) == 0 && rr.Next(noLeft) == 2, "a screen switched off is skipped");
        Check(rr.Next(none3) == -1, "no screen on: no view");
        RoundRobin after;
        after.Resume(1);
        Check(after.Next(all) == 2 && after.Next(all) == 0, "resume continues after the view just shown");
    }
    {
        char label[32];
        Check(MovingEveryChoice(9, label, sizeof label) == 2 && !label[0], "a preset N selects its entry");
        Check(MovingEveryChoice(0, label, sizeof label) == 0 && !label[0], "0 is 'only when stopped'");
        Check(MovingEveryChoice(7, label, sizeof label) == kMovingEveryPresetCount &&
                  !std::strcmp(label, "1 in 7 (custom)"),
            "a hand-set N shows as '1 in 7 (custom)'");
    }
    std::printf("%d vehicle monitor checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
