// Equivalence proof for the shared vehicle_family seat predicates of Halo 3, ODST and Reach:
// frozen copies of each title's own earlier predicates are compared with the shared ones for every
// identity, seat -2..17, mounted on/off and follow on/off - at compile time
// (static_assert) and again at run time so a failure names the predicate.
// Header-only; built as halomccvr_vehicle_family_tests. Pure logic: no game process.
#include "../src/common/titles/h3/halo3_vehicle_logic.h"
#include "../src/common/titles/odst/odst_vehicle_logic.h"
#include "../src/common/titles/reach/reach_vehicle_logic.h"
#include <cstdio>

namespace before
{
    // ---- Halo 3 (frozen copy) ----
    constexpr const Halo3SeatPoint* H3Find(Halo3VehicleId vehicle, int seatIndex, bool mountedTurret)
    {
        if (vehicle == Halo3VehicleId::Unknown)
            return nullptr;
        for (const Halo3SeatPoint& p : kHalo3SeatPoints)
            if (p.vehicle == vehicle && p.carrierFrame == mountedTurret && static_cast<int>(p.seatIndex) == seatIndex)
                return &p;
        return nullptr;
    }
    constexpr bool H3Driver(Halo3VehicleId id, int seatIndex, bool mountedTurret)
    {
        return !mountedTurret && seatIndex == 0 && id != Halo3VehicleId::Unknown &&
               id != Halo3VehicleId::StationaryTurret;
    }
    constexpr bool H3Look(Halo3VehicleId id)
    {
        switch (id)
        {
        case Halo3VehicleId::Warthog:
        case Halo3VehicleId::Mongoose:
        case Halo3VehicleId::Ghost:
        case Halo3VehicleId::Mauler:
        case Halo3VehicleId::Chopper:
        case Halo3VehicleId::Banshee:
        case Halo3VehicleId::Hornet:
            return true;
        default:
            return false;
        }
    }
    constexpr bool H3Wheel(Halo3VehicleId id)
    {
        return H3Look(id) && id != Halo3VehicleId::Banshee && id != Halo3VehicleId::Hornet;
    }
    constexpr bool H3Hull(Halo3VehicleId id, int seatIndex, bool mountedTurret)
    {
        if (id == Halo3VehicleId::Unknown || id == Halo3VehicleId::StationaryTurret)
            return false;
        return H3Find(id, seatIndex, mountedTurret) != nullptr;
    }
    constexpr bool H3Pitch(Halo3VehicleId id, int seatIndex, bool mountedTurret)
    {
        if (!H3Hull(id, seatIndex, mountedTurret))
            return false;
        return id != Halo3VehicleId::Banshee && id != Halo3VehicleId::Hornet;
    }
    constexpr bool H3Turret(Halo3VehicleId id, bool mountedTurret)
    {
        return mountedTurret || id == Halo3VehicleId::StationaryTurret;
    }
    constexpr bool H3Authors(Halo3VehicleId id, int seatIndex, bool mountedTurret, bool followEnabled)
    {
        return followEnabled && H3Driver(id, seatIndex, mountedTurret) && H3Look(id);
    }

    // ---- ODST (frozen copy) ----
    constexpr const OdstSeatPoint* OdFind(OdstVehicleId vehicle, int seatIndex, bool mountedTurret)
    {
        if (vehicle == OdstVehicleId::Unknown)
            return nullptr;
        for (const OdstSeatPoint& p : kOdstSeatPoints)
            if (p.vehicle == vehicle && p.carrierFrame == mountedTurret && static_cast<int>(p.seatIndex) == seatIndex)
                return &p;
        return nullptr;
    }
    constexpr bool OdDriver(OdstVehicleId id, int seatIndex, bool mountedTurret)
    {
        return !mountedTurret && seatIndex == 0 && id != OdstVehicleId::Unknown &&
               id != OdstVehicleId::StationaryTurret && id != OdstVehicleId::Shade;
    }
    constexpr bool OdLook(OdstVehicleId id)
    {
        switch (id)
        {
        case OdstVehicleId::Warthog:
        case OdstVehicleId::Mongoose:
        case OdstVehicleId::Ghost:
        case OdstVehicleId::Mauler:
        case OdstVehicleId::Chopper:
        case OdstVehicleId::Banshee:
        case OdstVehicleId::Hornet:
            return true;
        default:
            return false;
        }
    }
    constexpr bool OdWheel(OdstVehicleId id)
    {
        return OdLook(id) && id != OdstVehicleId::Banshee && id != OdstVehicleId::Hornet;
    }
    constexpr bool OdHull(OdstVehicleId id, int seatIndex, bool mountedTurret)
    {
        if (id == OdstVehicleId::Unknown || id == OdstVehicleId::StationaryTurret)
            return false;
        if (id == OdstVehicleId::Shade)
            return false;
        return OdFind(id, seatIndex, mountedTurret) != nullptr;
    }
    constexpr bool OdPitch(OdstVehicleId id, int seatIndex, bool mountedTurret)
    {
        if (!OdHull(id, seatIndex, mountedTurret))
            return false;
        return id != OdstVehicleId::Banshee && id != OdstVehicleId::Hornet;
    }
    constexpr bool OdTurret(OdstVehicleId id, bool mountedTurret)
    {
        if (mountedTurret)
            return true;
        return id == OdstVehicleId::StationaryTurret || id == OdstVehicleId::Shade;
    }
    constexpr bool OdAuthors(OdstVehicleId id, int seatIndex, bool mountedTurret, bool followEnabled)
    {
        return followEnabled && OdDriver(id, seatIndex, mountedTurret) && OdLook(id);
    }

    // ---- Reach compositions (frozen copy); the primitives are not shared ----
    constexpr bool RePitch(ReachVehicleId id, int seat)
    {
        return ReachVehicleSeatFollowsHull(id, seat) && !ReachVehicleIsAircraft(id);
    }
    constexpr bool ReAuthors(ReachVehicleId id, int seat, bool followEnabled)
    {
        return followEnabled && ReachVehicleSeatIsDriver(id, seat) && ReachVehicleIsLookSteered(id);
    }
    constexpr bool ReWheel(ReachVehicleId id)
    {
        return ReachVehicleIsLookSteered(id) && !ReachVehicleIsAircraft(id);
    }
} // namespace before

// The first predicate (1-based) that disagrees for any input, or 0.
constexpr int H3Mismatch()
{
    for (int raw = 0; raw <= static_cast<int>(Halo3VehicleId::StationaryTurret) + 1; ++raw)
    {
        const auto id = static_cast<Halo3VehicleId>(raw);
        if (Halo3VehicleIsLookSteered(id) != before::H3Look(id))
            return 1;
        if (Halo3VehicleUsesWheel(id) != before::H3Wheel(id))
            return 2;
        for (int mounted = 0; mounted < 2; ++mounted)
        {
            if (Halo3SeatIsTurret(id, mounted) != before::H3Turret(id, mounted))
                return 3;
            for (int seat = -2; seat <= 17; ++seat)
            {
                if (Halo3FindSeatPoint(id, seat, mounted) != before::H3Find(id, seat, mounted))
                    return 4;
                if (Halo3SeatIsDriver(id, seat, mounted) != before::H3Driver(id, seat, mounted))
                    return 5;
                if (Halo3SeatFollowsHull(id, seat, mounted) != before::H3Hull(id, seat, mounted))
                    return 6;
                if (Halo3SeatFollowsPitch(id, seat, mounted) != before::H3Pitch(id, seat, mounted))
                    return 7;
                for (int follow = 0; follow < 2; ++follow)
                    if (Halo3SeatAuthorsSteering(id, seat, mounted, follow) !=
                        before::H3Authors(id, seat, mounted, follow))
                        return 8;
            }
        }
    }
    return 0;
}
constexpr int OdstMismatch()
{
    for (int raw = 0; raw <= static_cast<int>(OdstVehicleId::Shade) + 1; ++raw)
    {
        const auto id = static_cast<OdstVehicleId>(raw);
        if (OdstVehicleIsLookSteered(id) != before::OdLook(id))
            return 1;
        if (OdstVehicleUsesWheel(id) != before::OdWheel(id))
            return 2;
        for (int mounted = 0; mounted < 2; ++mounted)
        {
            if (OdstSeatIsTurret(id, mounted) != before::OdTurret(id, mounted))
                return 3;
            for (int seat = -2; seat <= 17; ++seat)
            {
                if (OdstFindSeatPoint(id, seat, mounted) != before::OdFind(id, seat, mounted))
                    return 4;
                if (OdstSeatIsDriver(id, seat, mounted) != before::OdDriver(id, seat, mounted))
                    return 5;
                if (OdstSeatFollowsHull(id, seat, mounted) != before::OdHull(id, seat, mounted))
                    return 6;
                if (OdstSeatFollowsPitch(id, seat, mounted) != before::OdPitch(id, seat, mounted))
                    return 7;
                for (int follow = 0; follow < 2; ++follow)
                    if (OdstSeatAuthorsSteering(id, seat, mounted, follow) !=
                        before::OdAuthors(id, seat, mounted, follow))
                        return 8;
            }
        }
    }
    return 0;
}
constexpr int ReachMismatch()
{
    for (int raw = 0; raw <= kReachVehicleIdentityCount + 1; ++raw)
    {
        const auto id = static_cast<ReachVehicleId>(raw);
        if (ReachVehicleUsesWheel(id) != before::ReWheel(id))
            return 1;
        for (int seat = -2; seat <= 17; ++seat)
        {
            if (ReachVehicleSeatFollowsPitch(id, seat) != before::RePitch(id, seat))
                return 2;
            for (int follow = 0; follow < 2; ++follow)
                if (ReachVehicleSeatAuthorsSteering(id, seat, follow) != before::ReAuthors(id, seat, follow))
                    return 3;
        }
    }
    return 0;
}

static_assert(H3Mismatch() == 0, "Halo 3 vehicle predicates changed by the fold");
static_assert(OdstMismatch() == 0, "ODST vehicle predicates changed by the fold");
static_assert(ReachMismatch() == 0, "Reach vehicle predicates changed by the fold");

int main()
{
    const int h3 = H3Mismatch(), odst = OdstMismatch(), reach = ReachMismatch();
    std::printf("Vehicle family fold: H3 mismatch=%d ODST mismatch=%d Reach mismatch=%d\n", h3, odst, reach);
    return h3 || odst || reach ? 1 : 0;
}
