// Lens anchored to the rendered weapon node: transform maths and the d_px proof projection.
// Tests lens_anchor.h against the weapon model catalog; header-only, built as halomccvr_lens_anchor_tests.
// Pure logic: no game process.
#include "../src/common/lens_anchor.h"
#include <cmath>
#include <cstdio>
#include <cstring>

static int g_failures = 0;
static void Check(bool ok, const char* what)
{
    if (!ok)
    {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}
static bool Near(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) <= eps;
}

static const weapon_model::Model* ModelOf(GameTitle title, const char* name)
{
    for (const auto& m : weapon_model::kModels)
        if (m.title == title && !std::strcmp(m.name, name))
            return &m;
    return nullptr;
}

static bool Place(const weapon_model::Model* m, uint32_t count, float scale, const float basis[9], const float pos[3],
    weapon_muzzle::Ray& out)
{
    weapon_muzzle::Palette palette{};
    weapon_muzzle::Receipt base{};
    base.title = m->title;
    base.identity = m->identity;
    const bool ok = lens_anchor::Fill(palette, m->title, m->identity, count, base,
        [&](const weapon_muzzle::Marker& marker, weapon_muzzle::Ray& ray)
        { return weapon_muzzle::Transform(marker, scale, basis, pos, ray); });
    out = palette.barrels[0].ray;
    return ok;
}

int main()
{
    const float identity[9]{1, 0, 0, 0, 1, 0, 0, 0, 1};
    // Yaw 90 degrees about +Z: node x -> world +y, node y -> world -x (columns are the node axes).
    const float yaw90[9]{0, 1, 0, -1, 0, 0, 0, 0, 1};
    const auto* br = ModelOf(GameTitle::Halo3, "fp_battle_rifle");
    const auto* sn = ModelOf(GameTitle::Halo3, "fp_sniper_rifle");
    Check(br && sn, "H3 battle rifle and sniper are in the catalog");
    if (!br || !sn)
        return 1;
    const auto* row = weapon_profile::KitLensNodeFor(GameTitle::Halo3, "fp_battle_rifle");
    Check(
        row && Near(row->x, 0.02757f) && Near(row->z, 0.08207f), "BR node row = kit centre minus the zero root offset");
    // Sniper: kit centre (0.0296,-0.0072,0.0701) minus root (0.05574,0,0.03724).
    const auto* srow = weapon_profile::KitLensNodeFor(GameTitle::Halo3, "fp_sniper_rifle");
    Check(srow && Near(srow->x, 0.0296f - 0.05574f, 2e-4f) && Near(srow->y, -0.0072f, 2e-4f) &&
              Near(srow->z, 0.0701f - 0.03724f, 2e-4f),
        "sniper node row is relative to its root node");

    const float pos[3]{10.0f, 20.0f, 30.0f};
    weapon_muzzle::Ray ray{};
    Check(Place(br, 4, 1.0f, identity, pos, ray), "identity node places");
    Check(Near(ray.position[0], 10.02757f) && Near(ray.position[1], 20.00022f) && Near(ray.position[2], 30.08207f),
        "lens = node origin + glass local (world units)");
    Check(Near(ray.direction[0], 1.0f) && Near(ray.up[2], 1.0f), "ray faces node forward, up node up");
    Check(Place(br, 4, 2.0f, identity, pos, ray) && Near(ray.position[0], 10.0f + 2 * 0.02757f) &&
              Near(ray.position[2], 30.0f + 2 * 0.08207f),
        "node scale scales the local offset");
    Check(Place(br, 4, 1.0f, yaw90, pos, ray) && Near(ray.position[0], 10.0f - 0.00022f) &&
              Near(ray.position[1], 20.0f + 0.02757f) && Near(ray.position[2], 30.08207f) &&
              Near(ray.direction[1], 1.0f),
        "node rotation turns the offset with the gun");
    // The lens follows the gun, not a pose: moving the node moves the lens by exactly the same amount.
    weapon_muzzle::Ray moved{};
    const float pos2[3]{11.0f, 18.0f, 30.5f};
    Place(br, 4, 1.0f, yaw90, pos, ray);
    Place(br, 4, 1.0f, yaw90, pos2, moved);
    Check(Near(moved.position[0] - ray.position[0], 1.0f) && Near(moved.position[1] - ray.position[1], -2.0f) &&
              Near(moved.position[2] - ray.position[2], 0.5f),
        "lens translates rigidly with the node");
    const float skew[9]{1, 0, 0, 0.5f, 1, 0, 0, 0, 1};
    Check(!Place(br, 4, 1.0f, skew, pos, ray), "non-orthonormal node basis is refused");
    Check(!Place(br, 0, 1.0f, identity, pos, ray), "no nodes refused");
    weapon_muzzle::Palette none{};
    Check(!lens_anchor::Fill(none, GameTitle::Halo3, 0x1234ull, 4, {},
              [&](const weapon_muzzle::Marker& m, weapon_muzzle::Ray& r)
              { return weapon_muzzle::Transform(m, 1.0f, identity, pos, r); }),
        "unknown model identity gives no lens");
    const auto* needler = ModelOf(GameTitle::Halo3, "fp_needler");
    if (needler)
        Check(!Place(needler, 4, 1.0f, identity, pos, ray), "weapon with no kit row gives no lens");

    const auto* dmr = weapon_profile::KitLensNodeFor(GameTitle::HaloReach, "dmr");
    Check(dmr && Near(dmr->x, 0.02018f) && Near(dmr->z, 0.07913f),
        "Reach DMR row = kit centre (root node 0 at the origin, identity)");
    Check(lens_anchor::Supported(GameTitle::Halo3ODST) && lens_anchor::Supported(GameTitle::HaloReach) &&
              lens_anchor::Supported(GameTitle::HaloCE) && !lens_anchor::Supported(GameTitle::None),
        "lens receipts cover H3, ODST, Reach and CE (own rig)");
    {
        // CE: the kit row is at "frame gun", not node 0; Fill reads the node it is given.
        const auto* ce = ModelOf(GameTitle::HaloCE, "sniper rifle");
        Check(ce != nullptr, "CE sniper is in the catalog");
        if (ce)
        {
            const auto* crow = weapon_profile::KitLensNodeFor(GameTitle::HaloCE, "sniper rifle");
            Check(crow && Near(crow->x, -0.03675f) && Near(crow->z, 0.02928f),
                "CE sniper node row relative to frame gun");
            unsigned seen = 99;
            weapon_muzzle::Palette lens{};
            weapon_muzzle::Receipt base{};
            base.title = GameTitle::HaloCE;
            base.identity = ce->identity;
            Check(lens_anchor::Fill(
                      lens, GameTitle::HaloCE, ce->identity, 8, base,
                      [&](const weapon_muzzle::Marker& m, weapon_muzzle::Ray& r)
                      {
                          seen = m.node;
                          return weapon_muzzle::Transform(m, 1.0f, identity, pos, r);
                      },
                      3) &&
                      seen == 3 && Near(lens.barrels[0].ray.position[0], 10.0f - 0.03675f),
                "CE: Fill places the glass at the given node");
            Check(!lens_anchor::Fill(
                      lens, GameTitle::HaloCE, ce->identity, 3, base,
                      [&](const weapon_muzzle::Marker& m, weapon_muzzle::Ray& r)
                      { return weapon_muzzle::Transform(m, 1.0f, identity, pos, r); }, 3),
                "CE: a node outside the palette is refused");
            seen = 99;
            (void)lens_anchor::Fill(lens, GameTitle::Halo3, br->identity, 4, base,
                [&](const weapon_muzzle::Marker& m, weapon_muzzle::Ray& r)
                {
                    seen = m.node;
                    return weapon_muzzle::Transform(m, 1.0f, identity, pos, r);
                });
            Check(seen == 0, "other titles keep node 0 by default");
        }
    }
    {
        // Project into tracking space with a transform that turns world +x into tracking -z (metres = units / unitsPerMetre).
        weapon_muzzle::Receipt r{};
        r.title = GameTitle::HaloReach;
        r.generation = 1;
        r.identity = 5;
        r.serial = 1;
        r.space = 1;
        r.unit = 1;
        r.weapon = 2;
        r.at = 1;
        r.timeNs = 1;
        r.ray.position[0] = 3;
        r.ray.direction[0] = 1;
        r.ray.up[2] = 1;
        contact_melee::TrackingToWorld t;
        t.unitsPerMetre = 2.0f;
        t.axis[0] = {0, 0, 1};
        t.axis[1] = {0, 1, 0};
        t.axis[2] = {-1, 0, 0};
        t.origin = {0, 0, 0};
        dual_reticle::ProjectedRay p{};
        Check(t.Valid() && lens_anchor::Project(r, t, p), "Reach receipt projects to tracking space");
        Check(Near(p.tracking.position[2], -1.5f), "world x 3 at 2 units/m is 1.5 m ahead (-z)");
        Check(lens_anchor::Current(p, GameTitle::HaloReach, 1, 1, 1, 1, 50, 0, false), "fresh receipt is current");
        Check(
            !lens_anchor::Current(p, GameTitle::HaloReach, 1, 1, 1, 1, 500, 0, false), "stale receipt is not current");
        Check(!lens_anchor::Current(p, GameTitle::HaloReach, 2, 1, 1, 1, 50, 0, false),
            "generation change is not current");
        Check(lens_anchor::Recent(p, GameTitle::HaloReach, 1, 1, 20, 0, false), "a receipt a frame behind is recent");
        Check(!lens_anchor::Recent(p, GameTitle::HaloReach, 1, 1, 60, 0, false) &&
                  !lens_anchor::Recent(p, GameTitle::HaloReach, 1, 2, 20, 0, false) &&
                  !lens_anchor::Recent(p, GameTitle::HaloReach, 2, 1, 20, 0, false) &&
                  !lens_anchor::Recent(p, GameTitle::HaloReach, 1, 1, 20, 0, true),
            "old, other-space, other-generation or other-handedness receipts are refused");
        using lens_anchor::ReadResult;
        const auto cls = [&](uint32_t gen, uint64_t space, uint64_t serial, uint64_t now, bool left)
        { return lens_anchor::Classify(p, GameTitle::HaloReach, gen, space, serial, 1, now, 0, left); };
        Check(cls(1, 1, 1, 50, false).result == ReadResult::Ok, "exact serial and time is Ok");
        Check(cls(1, 1, 9, 40, false).result == ReadResult::Ok && cls(1, 1, 9, 40, false).ageMs == 39,
            "39 ms old with another serial is Ok (the old 25 ms window refused it)");
        Check(cls(1, 1, 9, 1 + lens_anchor::kRecentMs, false).result == ReadResult::Ok, "exactly kRecentMs old is Ok");
        Check(cls(1, 1, 9, 2 + lens_anchor::kRecentMs, false).result == ReadResult::Stale,
            "one ms past kRecentMs is Stale");
        Check(cls(1, 2, 9, 20, false).result == ReadResult::WrongSpace && !cls(1, 2, 9, 20, false).spaceMatch,
            "other space is WrongSpace");
        Check(cls(2, 1, 9, 20, false).result == ReadResult::WrongGun, "other generation is WrongGun");
        Check(cls(1, 1, 9, 20, true).result == ReadResult::WrongGun, "other handedness is WrongGun");
        Check(lens_anchor::Classify({}, GameTitle::HaloReach, 1, 1, 1, 1, 20, 0, false).result == ReadResult::NoReceipt,
            "empty receipt is NoReceipt");
        Check(lens_anchor::Classify(p, GameTitle::Halo3, 1, 1, 1, 1, 20, 0, false).result == ReadResult::WrongGun,
            "other title is WrongGun");
    }

    // d_px proof projection: 90 degree square eye, 1000 px, looking down -Z.
    lens_anchor::Eye eye;
    eye.tanLeft = eye.tanDown = -1;
    eye.tanRight = eye.tanUp = 1;
    eye.width = eye.height = 1000;
    float x = 0, y = 0;
    const float ahead[3]{0, 0, -1}, rightOf[3]{0.0762f, 0, -1}, behind[3]{0, 0, 1};
    Check(lens_anchor::ProjectPx(eye, ahead, x, y) && Near(x, 500, 0.01f) && Near(y, 500, 0.01f),
        "straight ahead projects to the centre");
    Check(lens_anchor::ProjectPx(eye, rightOf, x, y) && Near(x, 500 + 38.1f, 0.05f),
        "3 inches right at 1 m is ~38 px at 90 deg / 1000 px");
    Check(!lens_anchor::ProjectPx(eye, behind, x, y), "behind the eye does not project");
    Check(Near(lens_anchor::DistancePx(eye, ahead, ahead), 0.0f), "same point is 0 px apart");
    Check(Near(lens_anchor::DistancePx(eye, ahead, rightOf), 38.1f, 0.05f), "d_px between lens and node point");
    Check(lens_anchor::DistancePx(eye, ahead, behind) < 0.0f, "invisible point gives negative d_px");
    // Eye pose: yawed 90 deg left (looking along -X) still sees a point on -X dead centre.
    lens_anchor::Eye yawed = eye;
    yawed.orientation[1] = std::sin(3.14159265f / 4);
    yawed.orientation[3] = std::cos(3.14159265f / 4);
    const float onX[3]{-1, 0, 0};
    Check(lens_anchor::ProjectPx(yawed, onX, x, y) && Near(x, 500, 0.05f) && Near(y, 500, 0.05f),
        "eye orientation is honoured");
    std::printf("Lens anchor: %d failures\n", g_failures);
    return g_failures ? 1 : 0;
}
