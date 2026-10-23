// Halo 2 vehicle view heading: keeps one room-to-world heading per vehicle occupation, with optional
// hull follow. Used by src/dll/titles/h2/halo2_observer_6dof.cpp and tests/core_tests.cpp.
// Pure logic: no Windows calls, no globals, no logging.
#pragma once
#include "halo2_render_logic.h"

// H2's native observer heading is aim feedback. Using it as the controller's
// world reference feeds the commanded turn back into the target itself.
// Keep one room-to-world heading for this occupation; optional vehicle follow
// adds the chassis heading, never the cannon/native look heading.
struct Halo2VehicleViewKey
{
    uint32_t generation{}, unit = UINT32_MAX, parent = UINT32_MAX, root = UINT32_MAX;
    int16_t seat = -1;
    uint64_t space{};
    bool operator==(const Halo2VehicleViewKey&) const = default;
};

struct Halo2VehicleViewState
{
    Halo2VehicleViewKey key{};
    bool active = false, follow = false;
    uint64_t timeMs{};
    float referenceYaw{}, hullReferenceYaw{}, lastYaw{};
    // Comfort: the hull yaw this frame folded into the view (0 unless the
    // same occupation kept following since the previous frame).
    float followDelta{};

    bool Build(const Halo2VehicleViewKey& next, const Halo2CameraBasis& stock, const Halo2CameraBasis& hull,
        bool followHull, uint64_t now, Halo2CameraBasis& output) noexcept
    {
        followDelta = 0.f;
        if (!next.generation || next.unit == UINT32_MAX || next.parent == UINT32_MAX || next.root == UINT32_MAX ||
            next.seat < 0 || !now || !Halo2ValidateCameraBasis(stock) || !Halo2ValidateCameraBasis(hull))
        {
            active = false;
            return false;
        }
        const float horizontal = std::hypot(hull.forward[0], hull.forward[1]);
        if (followHull && horizontal < 1.e-4f)
        {
            active = false;
            return false;
        }
        const float hullYaw = std::atan2(hull.forward[1], hull.forward[0]);
        const bool same = active && key == next && now >= timeMs && now - timeMs <= 250;
        if (!same)
        {
            referenceYaw = std::atan2(stock.forward[1], stock.forward[0]);
            hullReferenceYaw = hullYaw;
        }
        else if (follow != followHull)
        {
            // Toggling follow preserves the current view at the toggle edge.
            referenceYaw = lastYaw;
            hullReferenceYaw = hullYaw;
        }
        const float previousYaw = lastYaw;
        const bool continuing = same && follow == followHull && followHull;
        lastYaw = referenceYaw + (followHull ? std::remainder(hullYaw - hullReferenceYaw, 6.28318530718f) : 0.f);
        followDelta = continuing ? std::remainder(lastYaw - previousYaw, 6.28318530718f) : 0.f;
        output = stock;
        output.forward[0] = std::cos(lastYaw);
        output.forward[1] = std::sin(lastYaw);
        output.forward[2] = 0;
        output.up[0] = output.up[1] = 0;
        output.up[2] = 1;
        key = next;
        active = true;
        follow = followHull;
        timeMs = now;
        return true;
    }
};
