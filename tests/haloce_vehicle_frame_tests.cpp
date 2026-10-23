// Halo CE occupied-vehicle frame validation (haloce_vehicle_frame.h): hull frames, seat anchors
// and the frozen turret reference. Header-only; built as halomccvr_haloce_vehicle_frame_tests.
// Pure logic: no game process.
#include "../src/common/titles/ce/haloce_vehicle_frame.h"
#include <cstdio>
#include <limits>

static int g_failures = 0;
static void Check(bool ok, const char* what)
{
    if (!ok)
    {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

int main()
{
    using namespace haloce_vehicle_frame;
    Frame f{};
    f.position = {10, 20, 1};
    f.forward = {1, 0, 0};
    f.up = {0, 0, 1};
    f.seat = 0;
    f.vehicle = 0xE1230004u;
    const Vec3 occupant{10.4f, 20.2f, 1.6f};
    Check(Valid(f, occupant), "driver seat next to the hull origin is valid");
    Frame g = f;
    g.seat = -1;
    Check(!Valid(g, occupant), "no seat: invalid");
    g = f;
    g.vehicle = UINT32_MAX;
    Check(!Valid(g, occupant), "no vehicle: invalid");
    g = f;
    g.forward = {0.5f, 0, 0};
    Check(!Valid(g, occupant), "non-unit forward: invalid");
    g = f;
    g.up = {1, 0, 0};
    Check(!Valid(g, occupant), "forward and up not orthogonal: invalid");
    g = f;
    g.position.x = std::numeric_limits<float>::quiet_NaN();
    Check(!Valid(g, occupant), "non-finite position: invalid");
    Check(!Valid(f, Vec3{30, 20, 1}), "occupant far from the hull: wrong position offset, invalid");
    Check(Valid(f, Vec3{13.5f, 20, 1}), "rear seat a few units back is still valid");
    {
        // CE keeps an attached object's position relative to its parent.
        Frame far = f;
        far.position = {100, 200, 5};
        Check(Valid(far, Vec3{0.4f, 0.2f, 0.6f}), "parent-relative occupant (small local offset) is valid");
        Check(Check(far, Vec3{40, 0, 0}) == Reject::OccupantFar, "occupant neither near nor local: refused");
        Frame skew = f;
        skew.up = {1, 0, 0};
        Check(Check(skew, occupant) == Reject::Basis, "refusal names the basis failure");
        Frame noSeat = f;
        noSeat.seat = -1;
        Check(Check(noSeat, occupant) == Reject::NoSeat, "refusal names the missing seat");
    }
    // Seat eye from the frozen seated body (the earlier CE VR mod).
    {
        Frame hull{};
        hull.position = {100, 200, 5};
        hull.forward = {0, 1, 0};
        hull.up = {0, 0, 1};
        hull.seat = 0;
        hull.vehicle = 0xE1230004u;
        hull.occupant = {0.3f, 0.1f, 0.2f};
        hull.occupantRelative = OccupantIsRelative(hull, hull.occupant);
        Check(hull.occupantRelative, "a small occupant far from the hull origin is parent-relative");
        Vec3 local{};
        Check(OccupantLocal(hull, local) && local.x == 0.3f && local.z == 0.2f, "relative occupant is the seat local");
        const auto driver = SeatEyeFor(kWarthogIdentity, 0);
        Check(driver.up == 0.72f && driver.forward == 0.10f,
            "Warthog driver eye = camera driver marker (+0.72 up, +0.10 ahead)");
        Check(SeatEyeFor(kWarthogIdentity, 1).up == 0.69f, "Warthog passenger eye = camera rider height (+0.69)");
        Check(SeatEyeFor(42, 0).up == 0.70f, "seats without an eye marker (Scorpion/Wraith driver): +0.70 m");
        // Scorpion driver seat z 1.287 m -> eye ~1.99 m; Wraith seat 1.643 m -> ~2.34 m (kit).
        Check(std::fabs(1.287f + SeatEyeFor(42, 0).up - 1.987f) < 1e-3f &&
                  std::fabs(1.643f + SeatEyeFor(42, 0).up - 2.343f) < 1e-3f,
            "kit eye heights for Scorpion/Wraith drivers");
        Vec3 eye{};
        const float wu = 1.0f / 3.048f;
        Check(SeatEye(hull, local, SeatEyeOffset{0, 0, 1.524f}, wu, eye) && std::fabs(eye.x - (100 - 0.1f)) < 1e-3f &&
                  std::fabs(eye.y - 200.3f) < 1e-3f && std::fabs(eye.z - (5 + 0.2f + 0.5f)) < 1e-3f,
            "eye = hull + basis*(seat local + offset): forward is hull +Y, left is -X, up is +Z");
        Frame world = hull;
        world.occupant = {100.1f, 200.4f, 5.3f};
        world.occupantRelative = OccupantIsRelative(world, world.occupant);
        Check(!world.occupantRelative && OccupantLocal(world, local) && std::fabs(local.x - 0.4f) < 1e-4f,
            "a world-space occupant is converted into hull coordinates");
    }
    // Motion proof of the position offset.
    {
        MotionProof proof;
        Vec3 pos{0, 0, 0};
        const Vec3 vel{0.2f, 0, 0}; // units per tick
        uint64_t ms = 1000;
        for (int i = 0; i < 12; ++i)
        {
            proof.Feed(7u, pos, vel, ms);
            ms += 100;
            pos.x += 0.2f * 3.0f; // 100 ms = 3 ticks
        }
        Check(proof.Current() == MotionProof::State::Proven, "position moving with the velocity proves the offset");
    }
    {
        MotionProof proof;
        uint64_t ms = 1000;
        for (int i = 0; i < 40; ++i)
        {
            proof.Feed(7u, Vec3{}, Vec3{0.2f, 0, 0}, ms);
            ms += 100;
        }
        Check(proof.Current() == MotionProof::State::Pending,
            "a field that never moves (zeros, or a paused game) gives no evidence; the reader stays stock");
    }
    {
        MotionProof proof;
        Vec3 pos{};
        uint64_t ms = 1000;
        for (int i = 0; i < 40; ++i)
        {
            proof.Feed(7u, pos, Vec3{0.2f, 0, 0}, ms);
            ms += 100;
            pos.y += 0.6f;
        }
        Check(proof.Current() == MotionProof::State::Refuted, "movement in the wrong direction is refuted");
    }
    {
        MotionProof proof;
        Vec3 pos{};
        uint64_t ms = 1000;
        for (int i = 0; i < 20; ++i)
        {
            proof.Feed(7u, pos, Vec3{0.001f, 0, 0}, ms);
            ms += 100;
        }
        Check(proof.Current() == MotionProof::State::Pending, "a parked vehicle gives no evidence either way");
        Vec3 p2{};
        uint64_t t2 = 1000;
        for (int i = 0; i < 20; ++i)
        {
            proof.Feed(7u, p2, Vec3{0.2f, 0, 0}, t2);
            t2 += 1000;
            p2.x += 6.0f;
        }
        Check(proof.Current() == MotionProof::State::Pending, "samples too far apart give no evidence");
    }
    {
        MotionProof proof;
        Vec3 pos{};
        uint64_t ms = 1000;
        for (int i = 0; i < 12; ++i)
        {
            proof.Feed(i % 2 ? 7u : 8u, pos, Vec3{0.2f, 0, 0}, ms);
            ms += 100;
            pos.x += 0.6f;
        }
        Check(proof.Current() == MotionProof::State::Pending, "switching vehicles restarts the baseline");
    }
    // The worker polls every 25-50 ms; the sim steps the position at 30 Hz.
    static const uint64_t kPolls[] = {25, 40, 50};
    for (const uint64_t pollMs : kPolls)
    {
        MotionProof proof;
        const Vec3 vel{0.3f, 0.1f, 0};
        for (uint64_t ms = 1000; ms < 5000 && proof.Current() == MotionProof::State::Pending; ms += pollMs)
        {
            const float ticks = float((ms * 3) / 100); // whole ticks elapsed at 30 Hz
            proof.Feed(7u, Vec3{vel.x * ticks, vel.y * ticks, 0}, vel, ms);
        }
        char what[96];
        std::snprintf(what, sizeof what, "%llu ms polls of a 30 Hz stepped position prove the offset",
            (unsigned long long)pollMs);
        Check(proof.Current() == MotionProof::State::Proven, what);
    }
    {
        // Seat anchor in vehicle coordinates round-trips and rides the hull.
        Frame fInner{{100, 50, 10}, {0, 1, 0}, {0, 0, 1}, 0, 7};
        Vec3 local{}, back{};
        Check(ToVehicleLocal(fInner, {101, 52, 11}, local) && FromVehicleLocal(fInner, local, back) &&
                  std::fabs(back.x - 101) < 1e-4f && std::fabs(back.y - 52) < 1e-4f && std::fabs(back.z - 11) < 1e-4f,
            "seat anchor round-trips through the vehicle frame");
        Check(std::fabs(local.x - 2) < 1e-4f && std::fabs(local.y + 1) < 1e-4f && std::fabs(local.z - 1) < 1e-4f,
            "vehicle-local axes: forward, left, up");
        Frame turned{{0, 0, 0}, {1, 0, 0}, {0, 0, 1}, 0, 7};
        Check(FromVehicleLocal(turned, local, back) && std::fabs(back.x - 2) < 1e-4f && std::fabs(back.y + 1) < 1e-4f,
            "a turned hull carries the seated anchor with it");
        Check(!ToVehicleLocal(fInner, {200, 50, 10}, local), "an anchor far outside the hull is refused");
        Frame bad{{0, 0, 0}, {0, 0, 0}, {0, 0, 1}, 0, 7};
        Check(!ToVehicleLocal(bad, {0, 0, 0}, local), "a degenerate frame is refused");
    }
    {
        // The frozen turret reference.
        Frame fInner{};
        fInner.vehicle = 7;
        fInner.seat = 2;
        fInner.forward = {1, 0, 0};
        fInner.up = {0, 0, 1};
        FrozenReference r;
        Vec3 cf{0, 1, 0}, cu{0, 0, 1}; // camera looking 90 deg left of the hull
        Check(r.Apply(fInner, 2, cf, cu) && std::fabs(cf.y - 1) < 1e-5f, "first apply captures the camera");
        Vec3 turned{1, 0, 0}, tu{0, 0, 1}; // the turret (and its camera) swung to the hull front
        Check(r.Apply(fInner, 2, turned, tu) && std::fabs(turned.y - 1) < 1e-5f && std::fabs(r.cameraYawDeg) < 1e-3f,
            "a turret yaw change leaves the reference alone");
        Frame gInner = fInner;
        gInner.forward = {0, 1, 0}; // hull turned 90 deg left
        Vec3 c2{0, 1, 0}, u2{0, 0, 1};
        Check(r.Apply(gInner, 2, c2, u2) && std::fabs(c2.x + 1) < 1e-5f, "a hull yaw carries the reference");
        Frame bad = fInner;
        bad.forward = {0, 0, 0};
        Vec3 c3{0.3f, 0.7f, 0}, u3{0, 0, 1};
        Check(!r.Apply(bad, 2, c3, u3) && c3.x == 0.3f && r.Valid(), "a bad frame keeps the reference and the camera");
        Vec3 c4{1, 0, 0}, u4{0, 0, 1};
        Check(r.Apply(fInner, 1, c4, u4) && std::fabs(c4.x - 1) < 1e-5f, "a seat change re-captures");
    }
    if (g_failures)
    {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("haloce_vehicle_frame_tests: all passed\n");
    return 0;
}
