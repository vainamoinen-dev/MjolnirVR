#pragma once
// Turret gyro: feeds the hull's own turn rate into a vehicle gun's aim so it holds its world direction.
// Used by src/dll/core/game.cpp; tested by tests/turret_gyro_tests.cpp.
// Pure logic: no globals, no game memory.
//
// Turret gyro (headset testing: without it, gunner aim bumps with the hull).
// A gun mounted on a vehicle is turned by the hull: when the hull yaws or
// pitches, the engine's aim moves with it and the closed aim loop only
// corrects it afterwards, at the gun's capped rate. The gyro adds the hull's
// own angular rate to the aim stick as feed-forward, so the aim holds its
// world direction through bumps and turns, like a stabilised mount.
// - Hull samples arrive at camera rate (yaw/pitch, Halo radians: yaw
//   counter-clockwise positive, pitch up positive); the rate comes from
//   consecutive samples and is held until a newer one arrives.
// - Yaw is about world up, so it carries 1:1. Hull pitch reaches the gun's
//   pitch by the cosine of the gun's angle off the hull nose (a gun facing
//   sideways gets roll instead, which a yaw/pitch stick cannot correct).
// - Stick out = rate / (gun's full-stick turn rate), scaled by strength.
//   Rates per title/turret are a table; a config scalar tunes them.
// - Safety: no sample, a stale one (> 100 ms), a seat change (generation) or
//   a jump over 30 deg between samples gives no compensation.
//
// Pure logic, no globals.
#include "titles/reach/reach_vehicle_logic.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace turret_gyro
{
    struct Settings
    {
        float strength{1.0f}; // 0 off .. 2
        float rateYaw{2.6f};  // gun turn rate at full stick, rad/s
        float ratePitch{2.6f};
        float deadbandDps{2.0f}; // ignore hull rates below this (sensor noise)
    };
    struct Hull
    {
        bool valid{};
        float yaw{}, pitch{};
        uint64_t sampleMs{};
        uint32_t generation{};
    };
    struct Output
    {
        bool active{};
        float rx{}, ry{};           // add to the aim stick (+rx turns right, +ry raises)
        float yawDps{}, pitchDps{}; // hull rates, for logs
    };

    inline constexpr float kPi = 3.14159265f, kRadToDeg = 57.2957795f;
    inline constexpr uint64_t kStaleMs = 100;
    inline constexpr float kJumpRad = 0.5236f; // 30 deg

    inline float WrapPi(float a) noexcept
    {
        while (a > kPi)
            a -= 2 * kPi;
        while (a < -kPi)
            a += 2 * kPi;
        return a;
    }

    class Gyro
    {
    public:
        void Reset() noexcept { *this = Gyro{}; }
        // aimYaw: the engine's current aim yaw (Halo radians), for the pitch share.
        Output Update(bool enabled, const Hull& h, float aimYaw, uint64_t nowMs, const Settings& s) noexcept
        {
            Output out{};
            const bool usable = enabled && h.valid && std::isfinite(h.yaw) && std::isfinite(h.pitch) && h.sampleMs &&
                                nowMs >= h.sampleMs && nowMs - h.sampleMs <= kStaleMs;
            if (!usable)
            {
                Reset();
                return out;
            }
            if (!have_ || h.generation != gen_)
            {
                have_ = true;
                gen_ = h.generation;
                lastYaw_ = h.yaw;
                lastPitch_ = h.pitch;
                lastMs_ = h.sampleMs;
                yawRate_ = pitchRate_ = 0.0f;
            }
            else if (h.sampleMs != lastMs_)
            {
                const float dYaw = WrapPi(h.yaw - lastYaw_), dPitch = h.pitch - lastPitch_;
                const float dt = std::max(0.002f, (h.sampleMs - lastMs_) / 1000.0f);
                if (std::fabs(dYaw) > kJumpRad || std::fabs(dPitch) > kJumpRad || h.sampleMs - lastMs_ > kStaleMs)
                    yawRate_ = pitchRate_ = 0.0f; // a teleport, seat change or gap: no rate
                else
                {
                    yawRate_ = dYaw / dt;
                    pitchRate_ = dPitch / dt;
                }
                lastYaw_ = h.yaw;
                lastPitch_ = h.pitch;
                lastMs_ = h.sampleMs;
            }
            const float strength = std::isfinite(s.strength) ? std::clamp(s.strength, 0.0f, 2.0f) : 1.0f;
            const float dead = (std::isfinite(s.deadbandDps) ? std::max(0.0f, s.deadbandDps) : 2.0f) / kRadToDeg;
            const float ry = std::isfinite(s.rateYaw) && s.rateYaw > 0.1f ? s.rateYaw : 2.6f;
            const float rp = std::isfinite(s.ratePitch) && s.ratePitch > 0.1f ? s.ratePitch : 2.6f;
            const float yawRate = std::fabs(yawRate_) < dead ? 0.0f : yawRate_;
            float pitchRate = std::fabs(pitchRate_) < dead ? 0.0f : pitchRate_;
            if (std::isfinite(aimYaw))
                pitchRate *= std::cos(WrapPi(aimYaw - h.yaw));
            // Hull turns left (+yaw) -> the gun is dragged left -> turn right (+rx).
            // Hull pitches up -> the gun is dragged up -> lower it (-ry).
            out.rx = std::clamp(strength * yawRate / ry, -1.0f, 1.0f);
            out.ry = std::clamp(-strength * pitchRate / rp, -1.0f, 1.0f);
            out.yawDps = yawRate_ * kRadToDeg;
            out.pitchDps = pitchRate_ * kRadToDeg;
            out.active = strength > 0.0f;
            if (!out.active)
                out.rx = out.ry = 0.0f;
            return out;
        }

    private:
        bool have_{};
        uint32_t gen_{};
        float lastYaw_{}, lastPitch_{}, yawRate_{}, pitchRate_{};
        uint64_t lastMs_{};
    };

    // Gun turn rate at full stick (rad/s), per title and turret. Reach attached
    // guns (Warthog chaingun/gauss/rocket, Wraith gunner, Scorpion AI gun, chin
    // guns) and walk-up guns riding a carrier (Falcon sides). Starting values:
    // MCC's default look rate; tuned at the headset through the config scalar.
    struct Rates
    {
        float yaw, pitch;
    };
    inline constexpr Rates kDefaultRates{2.6f, 2.6f};
    struct ReachRateRow
    {
        ReachVehicleId id;
        Rates rates;
    };
    inline constexpr ReachRateRow kReachRates[]{
        {ReachVehicleId::WarthogChaingun, {2.6f, 2.6f}}, {ReachVehicleId::WarthogGauss, {2.6f, 2.6f}},
        {ReachVehicleId::WarthogRocket, {2.6f, 2.6f}}, {ReachVehicleId::WraithGunner, {2.6f, 2.6f}},
        {ReachVehicleId::ScorpionAntiInfantry, {2.6f, 2.6f}}, {ReachVehicleId::PelicanChinGun, {2.6f, 2.6f}},
        {ReachVehicleId::SpacePhantomChinGun, {2.6f, 2.6f}}, {ReachVehicleId::SpacePhantomBeamTurret, {2.6f, 2.6f}},
        {ReachVehicleId::PlasmaTurret, {2.6f, 2.6f}}, // Falcon side guns ride a carrier
    };
    // Halo 2's aim servo learns each axis' stick sign from evidence (capped at
    // `cap`); its gyro waits for half the cap so a wrong first guess cannot add
    // to the hull drag instead of cancelling it.
    inline bool DirectionLearned(float evidence, float cap) noexcept
    {
        return std::isfinite(evidence) && std::isfinite(cap) && cap > 0.0f && std::fabs(evidence) >= 0.5f * cap;
    }

    // Halo 3 / ODST mounted gunners (shared numbering: 2 = Warthog). H3EK seat
    // blocks: warthog_g turns 60 deg/s in yaw with no pitch cap.
    struct LegacyRateRow
    {
        int vehicleId;
        Rates rates;
    };
    inline constexpr LegacyRateRow kLegacyRates[]{
        {2, {1.047f, 2.6f}},
    };
    inline float Scale(float scale) noexcept
    {
        return std::isfinite(scale) ? std::clamp(scale, 0.25f, 4.0f) : 1.0f;
    }
    inline Rates LegacyRatesFor(int vehicleId, float scale) noexcept
    {
        Rates r = kDefaultRates;
        for (const LegacyRateRow& row : kLegacyRates)
            if (row.vehicleId == vehicleId)
            {
                r = row.rates;
                break;
            }
        const float k = Scale(scale);
        return {r.yaw * k, r.pitch * k};
    }
    // Rates for a Reach gun, times the config scalar (0.25..4).
    inline Rates ReachRatesFor(ReachVehicleId id, float scale) noexcept
    {
        Rates r = kDefaultRates;
        for (const ReachRateRow& row : kReachRates)
            if (row.id == id)
            {
                r = row.rates;
                break;
            }
        const float k = Scale(scale);
        return {r.yaw * k, r.pitch * k};
    }
} // namespace turret_gyro
