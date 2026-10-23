#pragma once
// Enclosed-cockpit screen: placement and camera rules for a display inside closed vehicle seats.
// Used by dll/core (game, vr, native vehicle first person), the Halo 2 observer, common/
// vehicle_monitors_logic.h and the cockpit and scope tests.
//
// Enclosed-cockpit screen: in a closed
// vehicle seat the eye sees only hull interior, so the cockpit gets a display
// showing the view from a forward camera on the hull. The camera is a second
// render through the existing scope third pass; the display is a quad fixed in
// the cockpit (the seated eye captured on entry), drawn as a framed HUD
// window (visor shader, cockpit mode).
//
// Pure logic: no globals, no game memory.
#include "titles/reach/reach_vehicle_logic.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cockpit_screen
{
    enum class Class : uint8_t
    {
        None,
        Tank,
        Flyer
    };

    // Reach driver/pilot seats that are enclosed (no view out of the seated eye).
    // Gunner turrets, open Warthog-style seats and passenger seats are not.
    inline constexpr Class ReachClassOf(ReachVehicleId id, int seat) noexcept
    {
        if (seat != 0)
            return Class::None;
        switch (id)
        {
        case ReachVehicleId::Scorpion:
        case ReachVehicleId::Wraith:
            return Class::Tank;
        case ReachVehicleId::Banshee:
        case ReachVehicleId::SpaceBanshee:
        case ReachVehicleId::Seraph:
            return Class::Flyer;
        default:
            return Class::None;
        }
    }

    // Forward/up of the hull camera from the seat anchor, metres. Tanks look past
    // the gun mantlet; flyers just ahead of the nose.
    // Halo 3 / ODST: the two share vehicle numbering (1 scorpion, 5 wraith, 7 banshee).
    // The driver/pilot seat of an enclosed vehicle only, never a mounted turret.
    inline constexpr Class LegacyClassOf(int vehicleId, int seat, bool mounted) noexcept
    {
        if (seat != 0 || mounted)
            return Class::None;
        return vehicleId == 1 || vehicleId == 5 ? Class::Tank : vehicleId == 7 ? Class::Flyer : Class::None;
    }
    // Halo 2: the vehicle model identity (halo2_vehicle_identity.h hashes) of the
    // enclosed Scorpion, Wraith and Banshee; seat 0 is the driver/pilot.
    inline constexpr Class Halo2ClassOf(uint64_t identity, int seat) noexcept
    {
        if (seat != 0)
            return Class::None;
        return identity == 0x1E6E302886CF3D7Bull || identity == 0x55D8FE797FA258C7ull ? Class::Tank
               : identity == 0x500C52F8DDCD350Full                                    ? Class::Flyer
                                                                                      : Class::None;
    }
    inline constexpr float kWorldUnitsPerMeter = 1.0f / 3.048f; // Halo: 1 world unit = 10 ft

    inline constexpr float CameraAheadM(Class c) noexcept
    {
        return c == Class::Tank ? 4.0f : c == Class::Flyer ? 3.0f : 0.0f;
    }
    inline constexpr float CameraUpM(Class c) noexcept
    {
        return c == Class::Tank ? 1.2f : c == Class::Flyer ? 0.6f : 0.0f;
    }

    inline bool Wanted(bool enabled, Class c, bool vehicleActive, bool hullBasisValid, bool cutscene) noexcept
    {
        return enabled && c != Class::None && vehicleActive && hullBasisValid && !cutscene;
    }

    // Camera pose in game-world units. base: the seat camera anchor; hullForward:
    // the hull's forward (any length, pitch allowed); aheadWorld/upWorld: the
    // class offsets already converted to world units. World up is +Z.
    inline bool CameraPose(const float base[3], const float hullForward[3], float aheadWorld, float upWorld,
        float outPosition[3], float outForward[3], float outUp[3]) noexcept
    {
        for (int i = 0; i < 3; ++i)
            if (!std::isfinite(base[i]) || !std::isfinite(hullForward[i]))
                return false;
        if (!std::isfinite(aheadWorld) || !std::isfinite(upWorld))
            return false;
        const float l = std::sqrt(
            hullForward[0] * hullForward[0] + hullForward[1] * hullForward[1] + hullForward[2] * hullForward[2]);
        if (!(l > 1e-4f))
            return false;
        const float f[3]{hullForward[0] / l, hullForward[1] / l, hullForward[2] / l};
        if (std::fabs(f[2]) > 0.98f)
            return false; // straight up/down: no horizon to level
        // up = world Z with its forward component removed.
        float u[3]{-f[2] * f[0], -f[2] * f[1], 1.0f - f[2] * f[2]};
        const float ul = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
        if (!(ul > 1e-4f))
            return false;
        for (int i = 0; i < 3; ++i)
        {
            outForward[i] = f[i];
            outUp[i] = u[i] / ul;
            outPosition[i] = base[i] + f[i] * aheadWorld;
        }
        outPosition[2] += upWorld;
        return true;
    }

    struct Lens
    {
        float horizontal = 0, vertical = 0;
    };
    // Half-angle tangents of the capture so the 4:3 display window (a centre crop of
    // the capture) shows `fovDeg` horizontally. Source wider than 4:3: the window
    // keeps the full height; otherwise the full width.
    inline Lens LensFor(float fovDeg, float sourceAspect) noexcept
    {
        const float fov = std::isfinite(fovDeg) ? std::clamp(fovDeg, 40.0f, 120.0f) : 90.0f;
        const float aspect =
            std::isfinite(sourceAspect) && sourceAspect > 0.5f && sourceAspect < 4.0f ? sourceAspect : 4.0f / 3.0f;
        const float windowH = std::tan(fov * 0.5f * 0.01745329252f);
        Lens l{};
        l.vertical = aspect >= 4.0f / 3.0f ? windowH * 0.75f : windowH / aspect;
        l.horizontal = l.vertical * aspect;
        return l;
    }

    struct Quad
    {
        float position[3]{};
        float orientation[4]{0, 0, 0, 1};
        float width = 0, height = 0;
        bool valid = false;
    };
    // The display, fixed in the cockpit: `distance` straight ahead of the captured
    // seated eye along its yaw, `offsetY` above (+) or below (-) the eye, 4:3.
    inline Quad Place(const float eye[3], float yaw, float widthM, float distanceM, float offsetYM) noexcept
    {
        Quad q{};
        if (!std::isfinite(eye[0]) || !std::isfinite(eye[1]) || !std::isfinite(eye[2]) || !std::isfinite(yaw))
            return q;
        const float w = std::isfinite(widthM) ? std::clamp(widthM, 0.15f, 1.50f) : 0.60f;
        const float d = std::isfinite(distanceM) ? std::clamp(distanceM, 0.40f, 2.00f) : 0.45f;
        const float y = std::isfinite(offsetYM) ? std::clamp(offsetYM, -0.60f, 0.60f) : 0.0f;
        // Local space: -z forward at yaw 0; a positive yaw about +Y turns left.
        q.position[0] = eye[0] - std::sin(yaw) * d;
        q.position[1] = eye[1] + y;
        q.position[2] = eye[2] - std::cos(yaw) * d;
        q.orientation[1] = std::sin(yaw * 0.5f);
        q.orientation[3] = std::cos(yaw * 0.5f);
        q.width = w;
        q.height = w * 0.75f;
        q.valid = true;
        return q;
    }

    // The seated eye the display is anchored to: captured when the screen first
    // appears and again when a position recenter lands.
    class Anchor
    {
    public:
        void Reset() noexcept { m_valid = false; }
        bool Valid() const noexcept { return m_valid; }
        // Returns true when this call captured a (new) anchor.
        bool Update(const float head[3], float yaw, uint32_t recenterEpoch) noexcept
        {
            if (m_valid && recenterEpoch == m_epoch)
                return false;
            if (!std::isfinite(head[0]) || !std::isfinite(head[1]) || !std::isfinite(head[2]) || !std::isfinite(yaw))
                return false;
            for (int i = 0; i < 3; ++i)
                m_eye[i] = head[i];
            m_yaw = yaw;
            m_epoch = recenterEpoch;
            m_valid = true;
            return true;
        }
        const float* Eye() const noexcept { return m_eye; }
        float Yaw() const noexcept { return m_yaw; }

    private:
        float m_eye[3]{};
        float m_yaw = 0;
        uint32_t m_epoch = 0;
        bool m_valid = false;
    };
} // namespace cockpit_screen
