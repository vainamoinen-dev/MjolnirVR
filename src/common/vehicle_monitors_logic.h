#pragma once
// Vehicle monitors: camera table, screen layout and round-robin scheduling for enclosed-seat screens.
// Used by src/dll/core (game.cpp, menu.cpp, vr.cpp) and the Halo 2 6DOF adapter; tested by
// tests/vehicle_monitors_tests.cpp. Pure logic: no globals, no rendering, no game memory.
//
// Vehicle monitors: three 4:3 screens fixed
// in an enclosed seat, showing a forward hull camera and two side cameras
// (same origin, yawed +-75 deg). One view is rendered per frame, round-robin.
// Pure logic: the per-vehicle camera table, seat names, view yaw, the screen
// layout and the round-robin scheduler. No globals.
#include "cockpit_screen_logic.h"
#include "titles/reach/reach_vehicle_logic.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace vehicle_monitors
{
    enum class Game : uint8_t
    {
        HaloCE,
        Halo2,
        Halo3Odst,
        Reach
    };
    enum class View : uint8_t
    {
        Forward = 0,
        Left = 1,
        Right = 2
    };
    inline constexpr int kViewCount = 3;
    inline constexpr const char* ViewName(View v) noexcept
    {
        return v == View::Left ? "left" : v == View::Right ? "right" : "forward";
    }

    // Monitor camera rows per vehicle seat. Metres; aheadM/upM from the seat
    // anchor along hull forward / world up. verified=false rows ("assumed",
    // "verify" or no seat marker) are used but log "camera row unverified" once.
    struct CameraRow
    {
        Game game;
        const char* vehicle;
        float aheadM, upM, sideYawDeg, fovDeg;
        bool verified;
    };
    // Reach: its scope pass culls with the union of the head and scope views from
    // one origin and refuses any ray more than 90 deg from the head's forward
    // (ExpandScopeCullTangents). A 75 deg side camera with a 60 deg FOV reaches
    // 105 deg and is refused while the player looks ahead, so Reach side cameras
    // turn 50 deg (edge under 90 deg even for a 16:9 capture, whose crop is wider than 4:3).
    inline constexpr float kReachSideYawDeg = 50.0f;
    inline constexpr CameraRow kRows[] = {
        {Game::HaloCE, "scorpion", 5.3f, 1.5f, 75, 60, true}, {Game::HaloCE, "wraith", 3.0f, 1.7f, 75, 60, true},
        {Game::HaloCE, "banshee", 2.9f, 0.6f, 75, 60, true}, {Game::Halo2, "scorpion", 5.3f, 1.5f, 75, 60, true},
        {Game::Halo2, "wraith", 3.1f, 1.7f, 75, 60, false}, // seat from the CE wraith marker
        {Game::Halo2, "banshee", 2.9f, 0.6f, 75, 60, true},
        {Game::Halo3Odst, "scorpion", 5.5f, 1.5f, 75, 60, false}, // seat from CE/H2 (no H3 driver marker)
        {Game::Halo3Odst, "wraith", 3.2f, 1.7f, 75, 60, false},   // seat from CE/H2
        {Game::Halo3Odst, "banshee", 2.9f, 0.6f, 75, 60, true},
        {Game::Reach, "scorpion", 5.6f, 1.5f, kReachSideYawDeg, 60, false}, // seat assumed as H2 (verify)
        {Game::Reach, "wraith", 2.3f, 2.0f, kReachSideYawDeg, 60, true},
        {Game::Reach, "banshee", 3.1f, 0.6f, kReachSideYawDeg, 60, false},       // seat assumed as H2
        {Game::Reach, "space_banshee", 3.0f, 0.6f, kReachSideYawDeg, 60, false}, // ahead "verify": flyer class value
        {Game::Reach, "seraph", 3.0f, 0.6f, kReachSideYawDeg, 60, false},        // bounds untrusted: flyer class values
    };
    inline constexpr int kRowCount = static_cast<int>(sizeof(kRows) / sizeof(kRows[0]));
    inline const CameraRow* RowFor(Game game, const char* vehicle) noexcept
    {
        if (!vehicle)
            return nullptr;
        for (const auto& r : kRows)
            if (r.game == game && !std::strcmp(r.vehicle, vehicle))
                return &r;
        return nullptr;
    }
    inline int RowIndex(const CameraRow* row) noexcept
    {
        return row ? static_cast<int>(row - kRows) : -1;
    }

    // Enclosed driver/pilot seats by name (the same seats cockpit_screen classifies).
    inline constexpr const char* ReachVehicleName(ReachVehicleId id, int seat) noexcept
    {
        if (seat != 0)
            return nullptr;
        switch (id)
        {
        case ReachVehicleId::Scorpion:
            return "scorpion";
        case ReachVehicleId::Wraith:
            return "wraith";
        case ReachVehicleId::Banshee:
            return "banshee";
        case ReachVehicleId::SpaceBanshee:
            return "space_banshee";
        case ReachVehicleId::Seraph:
            return "seraph";
        default:
            return nullptr;
        }
    }
    // Halo 3 / ODST vehicle ids (LegacyClassOf).
    inline constexpr const char* LegacyVehicleName(int vehicleId, int seat, bool mounted) noexcept
    {
        if (seat != 0 || mounted)
            return nullptr;
        return vehicleId == 1 ? "scorpion" : vehicleId == 5 ? "wraith" : vehicleId == 7 ? "banshee" : nullptr;
    }
    inline constexpr const char* Halo2VehicleName(uint64_t identity, int seat) noexcept
    {
        if (seat != 0)
            return nullptr;
        return identity == 0x1E6E302886CF3D7Bull   ? "scorpion"
               : identity == 0x55D8FE797FA258C7ull ? "wraith"
               : identity == 0x500C52F8DDCD350Full ? "banshee"
                                                   : nullptr;
    }

    // Halo CE: model identities (haloce_vehicle_identity.inl, FNV of the gbxmodel
    // node names, computed from the CE model node table). Seat 0 is
    // the driver/pilot. CE has no cockpit_screen classifier, so the class is here.
    inline constexpr uint64_t kHaloCEScorpionIdentity = 0xAD26FE6A4A70D93Aull; // scorpion + scorpion_mp
    inline constexpr uint64_t kHaloCEWraithIdentity = 0x0EC094FCF8FD97E9ull;
    inline constexpr uint64_t kHaloCEBansheeIdentity = 0xA2F12D01B571110Aull;
    inline constexpr const char* HaloCEVehicleName(uint64_t identity, int seat) noexcept
    {
        if (seat != 0)
            return nullptr;
        return identity == kHaloCEScorpionIdentity  ? "scorpion"
               : identity == kHaloCEWraithIdentity  ? "wraith"
               : identity == kHaloCEBansheeIdentity ? "banshee"
                                                    : nullptr;
    }
    inline constexpr cockpit_screen::Class HaloCEClassOf(uint64_t identity, int seat) noexcept
    {
        const char* name = HaloCEVehicleName(identity, seat);
        return !name            ? cockpit_screen::Class::None
               : name[0] == 'b' ? cockpit_screen::Class::Flyer
                                : cockpit_screen::Class::Tank;
    }

    // The camera for one view. Forward: the row's (or the class's) ahead/up; side
    // views share the origin and turn by the row's side yaw (+ = left) about the
    // camera up. fovDeg is the view's horizontal field of view.
    struct CameraSpec
    {
        float aheadM{}, upM{}, yawDeg{}, fovDeg{};
    };
    inline CameraSpec SpecFor(const CameraRow* row, cockpit_screen::Class cls, View view) noexcept
    {
        CameraSpec s{};
        s.aheadM = row ? row->aheadM : cockpit_screen::CameraAheadM(cls);
        s.upM = row ? row->upM : cockpit_screen::CameraUpM(cls);
        const float side = row ? row->sideYawDeg : 75.0f;
        s.yawDeg = view == View::Left ? side : view == View::Right ? -side : 0.0f;
        s.fovDeg = row ? row->fovDeg : 60.0f;
        return s;
    }

    // Rotate forward about up by yawDeg (+ = left with world up +Z, x forward,
    // y left). Both unit, perpendicular (cockpit_screen::CameraPose output).
    inline void YawAboutUp(float forward[3], const float up[3], float yawDeg) noexcept
    {
        if (!std::isfinite(yawDeg) || yawDeg == 0.0f)
            return;
        const float a = yawDeg * 0.01745329252f, c = std::cos(a), s = std::sin(a);
        // left = up x forward
        const float l[3]{up[1] * forward[2] - up[2] * forward[1], up[2] * forward[0] - up[0] * forward[2],
            up[0] * forward[1] - up[1] * forward[0]};
        for (int i = 0; i < 3; ++i)
            forward[i] = forward[i] * c + l[i] * s;
    }

    // Screen layout in the seat-anchor frame (LOCAL space, -z forward at yaw 0,
    // + yaw turns left). Centre screen straight ahead; side screens centred at
    // +-sideDeg around the eye and turned to face it; the top edge kTopBelowDeg
    // below the eye line.
    inline constexpr float kWidthM = 0.42f, kDistanceM = 0.60f, kSideDeg = 38.0f, kTopBelowDeg = 5.0f;
    inline cockpit_screen::Quad Place(
        const float eye[3], float seatYaw, View view, float widthM = kWidthM, float distanceM = kDistanceM) noexcept
    {
        const float w = std::isfinite(widthM) ? std::clamp(widthM, 0.20f, 0.80f) : kWidthM;
        const float d = std::isfinite(distanceM) ? std::clamp(distanceM, 0.40f, 1.20f) : kDistanceM;
        const float h = w * 0.75f;
        const float top = -std::tan(kTopBelowDeg * 0.01745329252f) * d;
        const float offset = view == View::Left ? kSideDeg : view == View::Right ? -kSideDeg : 0.0f;
        cockpit_screen::Quad q = cockpit_screen::Place(eye, seatYaw + offset * 0.01745329252f, w, d, 0.0f);
        if (!q.valid)
            return q;
        q.position[1] += top - h * 0.5f;
        q.height = h;
        return q;
    }

    // F1 "Vehicle screens while driving" (CE Anniversary): the preset
    // choices, plus the cfg's own value as a fifth "custom" entry when it was set
    // by hand to something else. Returns the selected index; customLabel gets
    // "1 in N (custom)" (or is emptied when the value is a preset).
    inline constexpr int kMovingEveryPresets[] = {0, 6, 9, 12};
    inline constexpr int kMovingEveryPresetCount = 4;
    inline int MovingEveryChoice(int value, char* customLabel, size_t customSize) noexcept
    {
        if (customLabel && customSize)
            customLabel[0] = 0;
        for (int i = 0; i < kMovingEveryPresetCount; ++i)
            if (value == kMovingEveryPresets[i])
                return i;
        if (customLabel && customSize)
            std::snprintf(customLabel, customSize, "1 in %d (custom)", value);
        return kMovingEveryPresetCount;
    }

    // Round-robin over the enabled views: one per rendered frame.
    class RoundRobin
    {
    public:
        // Returns the next enabled view after the last one, or -1 when none is on.
        int Next(const bool enabled[kViewCount]) noexcept
        {
            for (int step = 1; step <= kViewCount; ++step)
            {
                const int v = (last_ + step) % kViewCount;
                if (enabled[v])
                {
                    last_ = v;
                    return v;
                }
            }
            return -1;
        }
        void Reset() noexcept { last_ = kViewCount - 1; }
        // A view the title keeps asking for without a captured image (refused by
        // its render pass) is skipped after this long, so it cannot starve the
        // other screens. Time, not a call count: titles build the pose every
        // frame (some twice) while the refresh divisor renders every Nth.
        static constexpr unsigned long long kStuckMs = 500;
        // Continue after `view` (the one just shown).
        void Resume(int view) noexcept { last_ = view >= 0 && view < kViewCount ? view : kViewCount - 1; }

    private:
        int last_ = kViewCount - 1; // so the first view is Forward
    };
} // namespace vehicle_monitors
