#pragma once
// Vehicle seat rules shared by Halo 3, ODST and Reach, parameterised by each title's traits struct.
// Used by the per-title vehicle logic headers under src/common/titles (h3, odst, reach).
// Pure constexpr logic: no globals, no game memory.
//
// Vehicle seat rules shared by Halo 3, ODST and Reach.
// Each title keeps its own VehicleId enum on purpose (an enum change in one
// title must never silently re-key another's seats) and supplies a traits
// struct with only what differs:
//   Id, IsLookSteered(id), IsAircraft(id), SeatIsDriver(id, seat...),
//   SeatFollowsHull(id, seat...)
// The composition rules below are the same in all three titles. Halo 3 and
// ODST additionally build their seat primitives from an authored seat-point
// table and three small id sets (the "Table" rules).
//
// Pure constexpr logic: no globals, no game memory.
#include <cstddef>
#include <cstdint>

namespace vehicle_family
{
    template <class Id>
    struct SeatPoint
    {
        Id vehicle;
        int8_t seatIndex;
        bool carrierFrame;
        float x, y, z;
    };

    template <class Id, size_t N>
    constexpr bool In(const Id (&set)[N], Id id)
    {
        for (const Id member : set)
            if (member == id)
                return true;
        return false;
    }

    // ---- composition rules (all three titles) ----

    // A wheel is a ground control: look-steered, and not an aircraft (whose stick
    // is a flight control and whose vertical axis is climb).
    template <class T>
    constexpr bool UsesWheel(typename T::Id id)
    {
        return T::IsLookSteered(id) && !T::IsAircraft(id);
    }

    // Aircraft keep the proven yaw-only follow; every other following seat takes
    // the roll-stable pitch frame.
    template <class T, class... Seat>
    constexpr bool SeatFollowsPitch(typename T::Id id, Seat... seat)
    {
        return T::SeatFollowsHull(id, seat...) && !T::IsAircraft(id);
    }

    // The one gate that hands a seat's steering to the wheel/stick author instead
    // of the closed-loop hand aim. It switches on exactly the condition that makes
    // the view follow the hull, which is why the follow toggle is an argument.
    template <class T, class... Seat>
    constexpr bool SeatAuthorsSteering(bool followEnabled, typename T::Id id, Seat... seat)
    {
        return followEnabled && T::SeatIsDriver(id, seat...) && T::IsLookSteered(id);
    }

    // ---- table rules (Halo 3 and ODST) ----
    // Traits additionally supply: kSeats (SeatPoint<Id> table), kNotDriven,
    // kNoFollow, kTurretBodies (Id sets). Id::Unknown never matches a seat.

    // `vehicle` is the seated parent's identity for a normal seat, or the
    // CARRIER's identity when the player is a mounted-turret gunner.
    template <class T>
    constexpr const SeatPoint<typename T::Id>* TableFindSeatPoint(
        typename T::Id vehicle, int seatIndex, bool mountedTurret)
    {
        if (vehicle == T::Id::Unknown)
            return nullptr;
        for (const auto& p : T::kSeats)
            if (p.vehicle == vehicle && p.carrierFrame == mountedTurret && static_cast<int>(p.seatIndex) == seatIndex)
                return &p;
        return nullptr;
    }

    // The driver is seat 0 of the vehicle itself; a mounted gunner is seat 0 of the
    // GUN, and the ids in kNotDriven (Unknown, walk-up turrets, ...) never steer.
    template <class T>
    constexpr bool TableSeatIsDriver(typename T::Id id, int seatIndex, bool mountedTurret)
    {
        return !mountedTurret && seatIndex == 0 && !In(T::kNotDriven, id);
    }

    // A seat may take the hull-follow view only with an authored seat point, and
    // never for kNoFollow ids (no frame, or a body that IS what its aim turns).
    template <class T>
    constexpr bool TableSeatFollowsHull(typename T::Id id, int seatIndex, bool mountedTurret)
    {
        return !In(T::kNoFollow, id) && TableFindSeatPoint<T>(id, seatIndex, mountedTurret) != nullptr;
    }

    // A physically slewing gun rather than the occupant's own weapon: any mounted
    // gunner, or an id in kTurretBodies.
    template <class T>
    constexpr bool TableSeatIsTurret(typename T::Id id, bool mountedTurret)
    {
        return mountedTurret || In(T::kTurretBodies, id);
    }
} // namespace vehicle_family
