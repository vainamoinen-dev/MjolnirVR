#pragma once
// Scope lens anchored to the RENDERED weapon: lens = T(node 0 of the committed first-person weapon
// palette) x glass_local (kit row, node frame). No aim pose, grip or hand assumption is involved; the
// only input is the kit row (tools/kits/gen_scope_lens_rows.py -> kKitLensNodes).
// Used by dll/core (game, vr), the CE and Halo 2 first-person code and the lens/muzzle tests.
#include "weapon_muzzle.h"
#include "weapon_profiles.h"
#include "dual_reticle_logic.h"
#include <cmath>
#include <cstdint>

namespace lens_anchor
{
    // Titles whose committed weapon palette publishes a lens receipt. CE: our own first-person rig's
    // committed palette at "frame gun".
    inline bool Supported(GameTitle title) noexcept
    {
        return title == GameTitle::Halo3 || title == GameTitle::Halo3ODST || title == GameTitle::HaloReach ||
               title == GameTitle::Halo2 || title == GameTitle::Halo4 || title == GameTitle::HaloCE;
    }

    // Same maths and freshness as dual_reticle::Project/Current, minus that feature's title list.
    inline bool Project(const weapon_muzzle::Receipt& source, const contact_melee::TrackingToWorld& transform,
        dual_reticle::ProjectedRay& output) noexcept
    {
        if (!Supported(source.title) || !source.generation || !source.identity || !source.serial || !source.space ||
            source.unit == UINT32_MAX || source.weapon == UINT32_MAX || source.slot > 1 || source.barrel > 1 ||
            !transform.Valid())
            return false;
        dual_reticle::ProjectedRay candidate{};
        candidate.source = source;
        const auto position =
            transform.Tracking({source.ray.position[0], source.ray.position[1], source.ray.position[2]});
        if (!contact_melee::Finite(position))
            return false;
        candidate.tracking.position[0] = position.x;
        candidate.tracking.position[1] = position.y;
        candidate.tracking.position[2] = position.z;
        const contact_melee::Point direction{source.ray.direction[0], source.ray.direction[1], source.ray.direction[2]};
        const contact_melee::Point up{source.ray.up[0], source.ray.up[1], source.ray.up[2]};
        if (!contact_melee::Finite(direction) || !contact_melee::Finite(up))
            return false;
        for (int axis = 0; axis < 3; ++axis)
        {
            candidate.tracking.direction[axis] = contact_melee::Dot(direction, transform.axis[axis]);
            candidate.tracking.up[axis] = contact_melee::Dot(up, transform.axis[axis]);
        }
        output = candidate;
        return true;
    }
    inline bool Current(const dual_reticle::ProjectedRay& value, GameTitle title, uint32_t generation, uint64_t space,
        uint64_t serial, int64_t timeNs, uint64_t now, unsigned slot, bool leftHanded) noexcept
    {
        const auto& source = value.source;
        return Supported(title) && source.serial == serial && source.timeNs == timeNs &&
               weapon_muzzle::Fresh(source, title, generation, source.unit, source.weapon, space, now, timeNs,
                   static_cast<uint8_t>(slot), source.barrel, leftHanded);
    }

    // Same gun, generation and tracking space, published within kRecentMs (no exact-frame serial match): the compositor may
    // read the lens a frame or two behind the committed palette; the position is still in the same space, only a little old.
    // 25 ms (the old value) is under two frames at 72 Hz (27.8 ms), so a receipt that was fine one frame earlier was refused as soon as
    // the game thread ran a frame ahead, which left Reach/H2 on the aim-derived lens (the session-5 headset log).
    inline constexpr uint64_t kRecentMs = 50;
    inline bool Recent(const dual_reticle::ProjectedRay& value, GameTitle title, uint32_t generation, uint64_t space,
        uint64_t now, unsigned slot, bool leftHanded) noexcept
    {
        const auto& s = value.source;
        return Supported(title) && title != GameTitle::None && s.title == title && generation &&
               s.generation == generation && s.unit != UINT32_MAX && s.weapon != UINT32_MAX && s.identity && space &&
               s.space == space && s.serial && s.at && now >= s.at && now - s.at <= kRecentMs && s.slot == slot &&
               s.barrel == 0 && s.leftHanded == leftHanded;
    }

    // Why the consumer did or did not take a published lens receipt. Ok means Current or Recent accepted it.
    enum class ReadResult : uint8_t
    {
        Ok = 0,
        NoReceipt,
        WrongGun,
        WrongSpace,
        Stale,
        NotCurrent
    };
    inline const char* ReadResultName(ReadResult r) noexcept
    {
        switch (r)
        {
        case ReadResult::Ok:
            return "ok";
        case ReadResult::NoReceipt:
            return "no lens receipt published (needs gun_barrel_aim, a muzzle row for this gun and the muzzle owner check)";
        case ReadResult::WrongGun:
            return "lens receipt is from another title, generation, slot or handedness";
        case ReadResult::WrongSpace:
            return "lens receipt is in another tracking space";
        case ReadResult::Stale:
            return "lens receipt older than the recent window";
        case ReadResult::NotCurrent:
            return "lens receipt not accepted (serial/time/unit/weapon differ)";
        }
        return "unknown";
    }
    struct ReadReport
    {
        ReadResult result{ReadResult::NoReceipt};
        int64_t ageMs{-1};
        bool serialMatch{}, spaceMatch{};
    };
    // One place for the accept/refuse rule and its reason, so the log names the check that failed.
    inline ReadReport Classify(const dual_reticle::ProjectedRay& value, GameTitle title, uint32_t generation,
        uint64_t space, uint64_t serial, int64_t timeNs, uint64_t now, unsigned slot, bool leftHanded) noexcept
    {
        ReadReport report;
        const auto& s = value.source;
        if (!s.identity)
            return report;
        report.ageMs = s.at && now >= s.at ? static_cast<int64_t>(now - s.at) : -1;
        report.serialMatch = s.serial == serial;
        report.spaceMatch = s.space == space;
        if (lens_anchor::Current(value, title, generation, space, serial, timeNs, now, slot, leftHanded) ||
            lens_anchor::Recent(value, title, generation, space, now, slot, leftHanded))
        {
            report.result = ReadResult::Ok;
            return report;
        }
        if (!Supported(title) || s.title != title || !generation || s.generation != generation || s.slot != slot ||
            s.leftHanded != leftHanded)
            report.result = ReadResult::WrongGun;
        else if (!space || !report.spaceMatch)
            report.result = ReadResult::WrongSpace;
        else if (report.ageMs < 0 || report.ageMs > static_cast<int64_t>(kRecentMs))
            report.result = ReadResult::Stale;
        else
            report.result = ReadResult::NotCurrent;
        return report;
    }

    // Fills palette.barrels[0] with the lens centre (same Receipt/Ray conventions and freshness rules as the
    // muzzle). xf(marker, ray) must transform the marker by the committed palette node (false skips).
    // node: the palette index the kit row is measured from (0 = the gun's root; CE's graph holds the arms
    // too, so CE passes its "frame gun" node).
    template <class NodeRay>
    bool Fill(weapon_muzzle::Palette& palette, GameTitle title, uint64_t identity, uint32_t count,
        const weapon_muzzle::Receipt& base, NodeRay xf, uint32_t node = 0)
    {
        const auto* model = weapon_model::Find(title, identity);
        const auto* row = model ? weapon_profile::KitLensNodeFor(title, model->name) : nullptr;
        if (!row || count == 0 || count > 64 || node >= count)
            return false;
        weapon_muzzle::Marker marker{};
        marker.title = title;
        marker.identity = identity;
        marker.nodeCount = static_cast<uint16_t>(count);
        marker.node = static_cast<decltype(marker.node)>(node);
        marker.barrel = 0;
        marker.position[0] = row->x;
        marker.position[1] = row->y;
        marker.position[2] = row->z;
        marker.forward[0] = 1.0f;
        marker.up[2] = 1.0f;
        weapon_muzzle::Ray ray{};
        if (!xf(marker, ray))
            return false;
        weapon_muzzle::Receipt receipt = base;
        receipt.barrel = 0;
        receipt.ray = ray;
        palette.barrels[0] = receipt;
        return true;
    }

    // OpenXR-style eye: pose in the space of the point, symmetric-or-not tangents (tan of the half angles).
    struct Eye
    {
        float position[3]{}, orientation[4]{0, 0, 0, 1};
        float tanLeft{-1}, tanRight{1}, tanUp{1}, tanDown{-1}; // XrFovf angles as tangents (left/down negative)
        int width{1}, height{1};
    };

    inline void RotateInverse(const float q[4], const float v[3], float out[3]) noexcept
    {
        const float x = -q[0], y = -q[1], z = -q[2], w = q[3]; // conjugate
        const float tx = 2 * (y * v[2] - z * v[1]), ty = 2 * (z * v[0] - x * v[2]), tz = 2 * (x * v[1] - y * v[0]);
        out[0] = v[0] + w * tx + (y * tz - z * ty);
        out[1] = v[1] + w * ty + (z * tx - x * tz);
        out[2] = v[2] + w * tz + (x * ty - y * tx);
    }

    // World/local point -> pixel in that eye's image (x right, y down). False behind the eye or non-finite.
    inline bool ProjectPx(const Eye& eye, const float point[3], float& px, float& py) noexcept
    {
        const float rel[3]{point[0] - eye.position[0], point[1] - eye.position[1], point[2] - eye.position[2]};
        float v[3];
        RotateInverse(eye.orientation, rel, v);
        if (!(v[2] < -1e-4f))
            return false;                                 // eye looks down -Z
        const float nx = v[0] / -v[2], ny = v[1] / -v[2]; // tangent coordinates
        const float u = (nx - eye.tanLeft) / (eye.tanRight - eye.tanLeft);
        const float t = (eye.tanUp - ny) / (eye.tanUp - eye.tanDown);
        px = u * eye.width;
        py = t * eye.height;
        return std::isfinite(px) && std::isfinite(py);
    }

    // Pixel distance between two points as seen by one eye; negative when either is not visible.
    inline float DistancePx(const Eye& eye, const float a[3], const float b[3]) noexcept
    {
        float ax, ay, bx, by;
        if (!ProjectPx(eye, a, ax, ay) || !ProjectPx(eye, b, bx, by))
            return -1.0f;
        return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by));
    }
} // namespace lens_anchor
