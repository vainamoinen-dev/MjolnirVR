// Halo CE occupied-vehicle frame: reads and proves the hull pose, then places the seated eye and the
// turret aim reference in vehicle coordinates.
// Used by src/dll/core (native_vehicle_first_person.cpp, vr.cpp), src/dll/titles/ce (haloce_controls.cpp,
// haloce_unit_control.cpp) and tests/haloce_vehicle_frame_tests.cpp. Pure logic: no Windows calls, no globals, no logging.
#pragma once
#include "../../math_types.h"
// Halo CE (MCC halo1.dll) occupied-vehicle frame: hull position, forward and
// up, plus the occupant's seat - the anchor the earlier CE VR mod uses for seated
// views instead of the live head marker.
//
// Object layout (64-bit retail; the earlier CE VR mod's 32-bit Objects.h has the same order
// at +0x5C/+0x68/+0x74/+0x80): position +0x18, velocity +0x24, forward +0x30,
// up +0x3C. forward/up are read by retail's vehicle basis builder +0xB45178
// (with velocity at +0x24) and already used by the CE vehicle turn [V]; the
// position offset follows from that layout [I], so each sample is also checked
// against the occupant's own position (same offset): a seated occupant is
// always within a few world units of its vehicle's origin.
//
// That check compares the inferred offset with itself, so the position offset
// is PROVEN by motion before any consumer may use it: while the vehicle moves,
// the change in +0x18 between close samples must match the velocity at +0x24
// (world units per tick, 30 ticks per second) in size and direction. The
// worker polls every 25-50 ms but the sim steps every 33 ms, so each check is
// measured over a >= 150 ms baseline of back-to-back samples (one tick of
// error then stays well inside the size band), and a baseline with no
// movement at all is skipped as "no tick / paused", not counted. A few
// agreeing samples prove it for the session; mostly disagreeing samples refute
// it for good (stock). Pure logic.
#include <cmath>
#include <cstdint>

namespace haloce_vehicle_frame
{
    using Vec3 = mvr_math::Vec3;

    struct Frame
    {
        Vec3 position, forward, up;
        int16_t seat = -1;
        uint32_t vehicle = UINT32_MAX;
        Vec3 occupant{};               // the seated unit's +0x18 as sampled
        bool occupantRelative = false; // +0x18 is parent-relative (CE attached object)
    };

    // World units (1 unit = 10 ft); the largest CE vehicle seat (Pelican/Scorpion)
    // sits well inside this of the hull origin.
    inline constexpr float kMaxSeatDistance = 4.0f;

    inline bool Finite(const Vec3& v) noexcept
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }
    inline float Dot(const Vec3& a, const Vec3& b) noexcept
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    inline constexpr float kTicksPerSecond = 30.0f;
    inline constexpr float kMinMovingSpeed = 0.02f;  // world units per tick (~1.8 m/s)
    inline constexpr uint64_t kMaxSampleGapMs = 250; // between consecutive samples
    inline constexpr uint64_t kBaselineMs = 150;     // measured span of one check
    inline constexpr int kProofSamples = 3;

    // Motion proof of the position offset (see the header comment).
    class MotionProof
    {
    public:
        enum class State
        {
            Pending,
            Proven,
            Refuted
        };
        // One sample of the occupied vehicle (position at the inferred offset,
        // velocity at the proven one). Different vehicle, a gap or a bad sample
        // restarts the baseline at this sample.
        void Feed(uint32_t vehicle, const Vec3& position, const Vec3& velocity, uint64_t nowMs) noexcept
        {
            if (state_ != State::Pending)
                return;
            const bool continuous = haveBase_ && vehicle == baseVehicle_ && nowMs > lastMs_ &&
                                    nowMs - lastMs_ <= kMaxSampleGapMs && Finite(position) && Finite(velocity);
            lastMs_ = nowMs;
            if (!continuous)
            {
                Restart(vehicle, position, velocity, nowMs);
                return;
            }
            speedSum_ += std::sqrt(Dot(velocity, velocity));
            velocitySum_ = {velocitySum_.x + velocity.x, velocitySum_.y + velocity.y, velocitySum_.z + velocity.z};
            ++velocityCount_;
            if (nowMs - baseMs_ < kBaselineMs)
                return;
            const float speed = speedSum_ / float(velocityCount_);
            const Vec3 moved{position.x - basePosition_.x, position.y - basePosition_.y, position.z - basePosition_.z};
            const float distance = std::sqrt(Dot(moved, moved));
            const float heading = std::sqrt(Dot(velocitySum_, velocitySum_));
            if (speed >= kMinMovingSpeed && distance > 1e-6f && heading > 1e-6f)
            {
                const float ticks = float(nowMs - baseMs_) * kTicksPerSecond / 1000.0f;
                const float ratio = distance / (speed * ticks);
                const float cosine = Dot(moved, velocitySum_) / (distance * heading);
                // Over >= 150 ms one tick of timing error is <= ~25%, so the band
                // is generous; a wrong field (a parent-relative vector, another
                // object's position) still fails on size or direction.
                if (ratio >= 0.5f && ratio <= 2.0f && cosine >= 0.8f)
                    ++agree_;
                else
                    ++disagree_;
                if (agree_ >= kProofSamples && agree_ >= 3 * disagree_)
                    state_ = State::Proven;
                else if (disagree_ >= kProofSamples * 2 && disagree_ > 3 * agree_)
                    state_ = State::Refuted;
            }
            // Parked, or no movement at all (no tick / paused): no evidence.
            Restart(vehicle, position, velocity, nowMs);
        }
        State Current() const noexcept { return state_; }
        int Agree() const noexcept { return agree_; }
        int Disagree() const noexcept { return disagree_; }

    private:
        void Restart(uint32_t vehicle, const Vec3& position, const Vec3& velocity, uint64_t nowMs) noexcept
        {
            haveBase_ = Finite(position) && Finite(velocity);
            baseVehicle_ = vehicle;
            baseMs_ = nowMs;
            basePosition_ = position;
            speedSum_ = std::sqrt(Dot(velocity, velocity));
            velocitySum_ = velocity;
            velocityCount_ = 1;
        }
        State state_ = State::Pending;
        bool haveBase_ = false;
        uint32_t baseVehicle_ = UINT32_MAX;
        uint64_t baseMs_ = 0, lastMs_ = 0;
        Vec3 basePosition_{}, velocitySum_{};
        float speedSum_ = 0;
        int velocityCount_ = 0;
        int agree_ = 0, disagree_ = 0;
    };

    // Why a sample was refused (logged and counted per reason).
    enum class Reject : uint8_t
    {
        None,
        NoSeat,
        NoVehicle,
        NonFinite,
        Basis,
        OccupantFar
    };
    inline const char* RejectName(Reject r) noexcept
    {
        switch (r)
        {
        case Reject::None:
            return "none";
        case Reject::NoSeat:
            return "no seat";
        case Reject::NoVehicle:
            return "no vehicle";
        case Reject::NonFinite:
            return "non-finite";
        case Reject::Basis:
            return "forward/up not an orthonormal pair";
        case Reject::OccupantFar:
            return "occupant far from the hull";
        }
        return "?";
    }

    // Checks a sampled frame. The occupant's +0x18 is accepted in either form CE
    // can store it: world space (within a few units of the hull origin) or, as the
    // engine keeps it for an object attached to a parent, relative to that parent
    // (in headset testing a world-only check refused every seated sample:
    // refused 39 -> 2170, never one frame).
    inline Reject Check(const Frame& f, const Vec3& occupantPosition) noexcept
    {
        if (f.seat < 0)
            return Reject::NoSeat;
        if (f.vehicle == UINT32_MAX)
            return Reject::NoVehicle;
        if (!Finite(f.position) || !Finite(f.forward) || !Finite(f.up) || !Finite(occupantPosition))
            return Reject::NonFinite;
        if (std::fabs(Dot(f.forward, f.forward) - 1) > 0.05f || std::fabs(Dot(f.up, f.up) - 1) > 0.05f ||
            std::fabs(Dot(f.forward, f.up)) > 0.05f)
            return Reject::Basis;
        const Vec3 d{
            f.position.x - occupantPosition.x, f.position.y - occupantPosition.y, f.position.z - occupantPosition.z};
        const float limit = kMaxSeatDistance * kMaxSeatDistance;
        const bool worldNear = Dot(d, d) <= limit;
        const bool parentRelative = Dot(occupantPosition, occupantPosition) <= limit;
        return worldNear || parentRelative ? Reject::None : Reject::OccupantFar;
    }

    // Which form a validated occupant +0x18 is in: relative to the parent when it
    // is small but not near the hull origin in world space.
    inline bool OccupantIsRelative(const Frame& f, const Vec3& occupant) noexcept
    {
        const Vec3 d{f.position.x - occupant.x, f.position.y - occupant.y, f.position.z - occupant.z};
        const float limit = kMaxSeatDistance * kMaxSeatDistance;
        return Dot(occupant, occupant) <= limit && Dot(d, d) > limit;
    }

    // True when the sampled frame is a plausible occupied-vehicle frame.
    inline bool Valid(const Frame& f, const Vec3& occupantPosition) noexcept
    {
        return Check(f, occupantPosition) == Reject::None;
    }
    // Seat anchor in vehicle coordinates (the earlier CE VR mod: the settled seated
    // position frozen in the hull frame). Axes: forward, left = up x forward,
    // up' = forward x left (re-orthogonalised, so a slightly skewed up is fine).
    inline bool Axes(const Frame& f, Vec3& fw, Vec3& left, Vec3& up) noexcept
    {
        const float fl = std::sqrt(Dot(f.forward, f.forward));
        if (!Finite(f.forward) || !Finite(f.up) || !std::isfinite(fl) || fl < 1e-4f)
            return false;
        fw = {f.forward.x / fl, f.forward.y / fl, f.forward.z / fl};
        left = {f.up.y * fw.z - f.up.z * fw.y, f.up.z * fw.x - f.up.x * fw.z, f.up.x * fw.y - f.up.y * fw.x};
        const float ll = std::sqrt(Dot(left, left));
        if (!std::isfinite(ll) || ll < 1e-4f)
            return false;
        left = {left.x / ll, left.y / ll, left.z / ll};
        up = {fw.y * left.z - fw.z * left.y, fw.z * left.x - fw.x * left.z, fw.x * left.y - fw.y * left.x};
        return true;
    }
    inline bool ToVehicleLocal(const Frame& f, const Vec3& world, Vec3& local) noexcept
    {
        Vec3 fw, left, up;
        if (!Finite(f.position) || !Finite(world) || !Axes(f, fw, left, up))
            return false;
        const Vec3 d{world.x - f.position.x, world.y - f.position.y, world.z - f.position.z};
        local = {Dot(d, fw), Dot(d, left), Dot(d, up)};
        return Dot(local, local) <= kMaxSeatDistance * kMaxSeatDistance;
    }
    // Seated body position in hull coordinates (forward, left, up), whichever form
    // the occupant was sampled in. the earlier CE VR mod uses this body point, not the
    // head marker: the seated biped's head marker sits low in the hull in CE.
    inline bool OccupantLocal(const Frame& f, Vec3& local) noexcept
    {
        if (f.occupantRelative)
        {
            if (!Finite(f.occupant))
                return false;
            local = f.occupant;
            return true;
        }
        return ToVehicleLocal(f, f.occupant, local);
    }

    // Eye offset from the seated body, metres (forward, left, up). The seated
    // biped is attached at the seat marker, so the body point IS the seat marker;
    // the CE model data: every CE seat that defines an eye puts it
    // about 0.70 m above the seat - Warthog driver camera +0.72 up / +0.10 ahead,
    // rider +0.69, Scorpion passengers +0.685 - and the Scorpion driver has no
    // camera marker at all (Wraith's is a chase cam), so they take the same
    // +0.70 m. Per-model trims still adjust any seat.
    struct SeatEyeOffset
    {
        float forward, left, up;
    };
    inline constexpr uint64_t kWarthogIdentity = 0xE2E54B251CFFE5EDull; // CE seat identity (read from a session log)
    inline constexpr SeatEyeOffset kGenericSeatEye{0.0f, 0.0f, 0.70f};
    inline constexpr SeatEyeOffset SeatEyeFor(uint64_t vehicleIdentity, int seat) noexcept
    {
        if (vehicleIdentity == kWarthogIdentity)
            return seat == 0   ? SeatEyeOffset{0.10f, 0.0f, 0.72f} // driver: camera driver marker
                   : seat == 1 ? SeatEyeOffset{0.0f, 0.0f, 0.69f}  // passenger: camera rider height
                               : kGenericSeatEye;                  // gunner: stands at the turret
        return kGenericSeatEye;
    }

    // Eye in world units: hull origin + basis * (seat body local + eye offset).
    inline bool SeatEye(const Frame& f, const Vec3& seatLocal, const SeatEyeOffset& offsetM, float worldUnitsPerMeter,
        Vec3& eye) noexcept;

    inline bool FromVehicleLocal(const Frame& f, const Vec3& local, Vec3& world) noexcept
    {
        Vec3 fw, left, up;
        if (!Finite(f.position) || !Finite(local) || !Axes(f, fw, left, up))
            return false;
        world = {f.position.x + fw.x * local.x + left.x * local.y + up.x * local.z,
            f.position.y + fw.y * local.x + left.y * local.y + up.y * local.z,
            f.position.z + fw.z * local.x + left.z * local.y + up.z * local.z};
        return Finite(world);
    }
    inline bool SeatEye(const Frame& f, const Vec3& seatLocal, const SeatEyeOffset& offsetM, float worldUnitsPerMeter,
        Vec3& eye) noexcept
    {
        if (!std::isfinite(worldUnitsPerMeter) || worldUnitsPerMeter <= 0)
            return false;
        const Vec3 local{seatLocal.x + offsetM.forward * worldUnitsPerMeter,
            seatLocal.y + offsetM.left * worldUnitsPerMeter, seatLocal.z + offsetM.up * worldUnitsPerMeter};
        return FromVehicleLocal(f, local, eye);
    }

    // Turret aim: a seat's aim reference (the native camera's forward/up)
    // frozen in hull coordinates at seat entry and re-placed on the moving hull,
    // so the turret turning (its camera rides it) never turns the reference, while
    // hull turns and bumps still carry it. A bad frame leaves the reference (and
    // the camera) alone; only a vehicle/seat change or Reset() re-captures.
    struct FrozenReference
    {
        uint32_t vehicle = UINT32_MAX;
        int16_t seat = -1;
        Vec3 forward{}, up{};           // hull-local (forward, left, up)
        Vec3 hullForward{}, hullLeft{}; // the last applied frame's axes (for logging)
        float cameraYawDeg = 0;         // the native camera's yaw in the hull (turret yaw)
        bool Valid() const noexcept { return vehicle != UINT32_MAX; }
        void Reset() noexcept { *this = FrozenReference{}; }
        bool Apply(const Frame& f, int16_t seatIndex, Vec3& cameraForward, Vec3& cameraUp) noexcept
        {
            Vec3 hf{}, hl{}, hu{};
            if (!Finite(cameraForward) || !Finite(cameraUp) || !Axes(f, hf, hl, hu))
                return false;
            const auto local = [&](const Vec3& v) { return Vec3{Dot(v, hf), Dot(v, hl), Dot(v, hu)}; };
            const auto world = [&](const Vec3& v)
            {
                return Vec3{hf.x * v.x + hl.x * v.y + hu.x * v.z, hf.y * v.x + hl.y * v.y + hu.y * v.z,
                    hf.z * v.x + hl.z * v.y + hu.z * v.z};
            };
            const Vec3 cameraLocal = local(cameraForward);
            if (vehicle != f.vehicle || seat != seatIndex)
            {
                vehicle = f.vehicle;
                seat = seatIndex;
                forward = cameraLocal;
                up = local(cameraUp);
            }
            hullForward = hf;
            hullLeft = hl;
            cameraYawDeg = std::atan2(cameraLocal.y, cameraLocal.x) * 57.2957795f;
            cameraForward = world(forward);
            cameraUp = world(up);
            return true;
        }
        // Yaw of a world direction in the last applied hull frame, degrees.
        float HullYawDeg(const Vec3& world) const noexcept
        {
            return std::atan2(Dot(world, hullLeft), Dot(world, hullForward)) * 57.2957795f;
        }
    };
} // namespace haloce_vehicle_frame
