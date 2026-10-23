#pragma once
// Halo 2 vehicle roles for the shared vehicle features (wheel, trigger
// throttle, view follow, flight stick). Seat flags come from the retail seat
// block exactly as the native camera selector +0x6D4C50 reads it (unit
// definition +0x1CC block address, stride 0xB0, flags dword +0); bit order
// matches H2EK unit seats: 2 driver, 3 gunner, 4 third-person camera.
// Vehicle classes come from the proven render-model identity names
// (halo2_vehicle_identity.h). Pure logic.
#include <cmath>
#include <cstdint>
#include <cstring>

namespace halo2_vehicle_roles
{
    inline constexpr uint32_t kSeatDriverBit = 1u << 2;
    inline constexpr uint32_t kSeatGunnerBit = 1u << 3;

    enum class Steer : uint8_t
    {
        None,
        Car,
        Bike
    };
    struct Role
    {
        Steer steer = Steer::None; // wheel / stick class for a look-steered driver
        bool aircraft = false;     // flight stick (Banshee)
        bool armedDriver = false;  // the driver fires the vehicle's guns
    };

    // By exact identity name (whole vehicle, not its parts).
    inline Role RoleFor(const char* name) noexcept
    {
        Role r{};
        if (!name)
            return r;
        if (!std::strcmp(name, "Warthog") || !std::strcmp(name, "Spectre"))
            r.steer = Steer::Car;
        else if (!std::strcmp(name, "Ghost"))
        {
            r.steer = Steer::Bike;
            r.armedDriver = true;
        }
        else if (!std::strcmp(name, "Mongoose"))
            r.steer = Steer::Bike;
        else if (!std::strcmp(name, "Banshee"))
        {
            r.aircraft = true;
            r.armedDriver = true;
        }
        return r;
    }

    // Stationary turrets are whole vehicles of their own (both AP turrets and the
    // Covenant gun tower), whatever their seat flags say.
    inline bool IsStationaryTurret(const char* name) noexcept
    {
        return name && (!std::strcmp(name, "C Turret Ap") || !std::strcmp(name, "H Turret Ap") ||
                           !std::strcmp(name, "Cov Guntower"));
    }

    // The seat's steering, given its native flags. Only a driver seat steers.
    // A turret seat aims a rate-capped gun (the shared turret mode: handles, the
    // parked servo and the reticle riding the barrel): a gunner seat that does not
    // drive (Warthog chaingun / gauss, Spectre), or a stationary turret. The
    // gunner's parent is the vehicle itself in Halo 2 (no separate turret object),
    // so the seat flags, not the parent, decide.
    struct Seat
    {
        bool driver = false, gunner = false, turret = false;
        Role role{};
    };
    inline Seat SeatFor(uint32_t flags, const char* vehicleName) noexcept
    {
        Seat s{};
        s.driver = (flags & kSeatDriverBit) != 0;
        s.gunner = (flags & kSeatGunnerBit) != 0;
        s.turret = (s.gunner && !s.driver) || IsStationaryTurret(vehicleName);
        if (s.driver && !s.turret)
            s.role = RoleFor(vehicleName);
        return s;
    }

    // The reticle that rides a rate-capped barrel, in VR tracking angles (yaw =
    // atan2(x, -z), positive to the right; pitch up-positive). Halo world yaw is
    // atan2(y, x), positive to the LEFT, and both frames share "up", so the
    // barrel's remaining error moves the hand ray by the same angles with yaw
    // negated. Once the gun has caught up, the reticle is the hand ray.
    inline void BarrelReticle(float handYaw, float handPitch, float desiredYaw, float desiredPitch, float engineYaw,
        float enginePitch, float& outYaw, float& outPitch) noexcept
    {
        constexpr float kPi = 3.14159265f;
        float yawError = engineYaw - desiredYaw;
        while (yawError > kPi)
            yawError -= 2.0f * kPi;
        while (yawError < -kPi)
            yawError += 2.0f * kPi;
        outYaw = handYaw - yawError;
        outPitch = handPitch + (enginePitch - desiredPitch);
        if (outPitch > 1.55f)
            outPitch = 1.55f;
        if (outPitch < -1.55f)
            outPitch = -1.55f;
    }
} // namespace halo2_vehicle_roles
