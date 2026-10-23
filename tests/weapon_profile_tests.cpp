// Weapon profile table: rows name real catalog models, unknown weapons and unset
// fields resolve to today's behaviour, set fields are returned as authored, and
// each title's class, scoped and dual flags are what the table claims (a change
// to a row is a behaviour change; update this test deliberately).
#include "../src/common/weapon_profiles.h"
#include <cmath>
#include "../src/common/weapon_tuning_export.h"
#include <string>
#include <cstdio>
#include <cstring>
#include <cstring>
#include <iterator>

static int g_failures = 0;
static void Check(bool ok, const char* what)
{
    if (!ok)
    {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

using namespace weapon_profile;

static const weapon_model::Model* ModelOf(GameTitle title, const char* name)
{
    for (const auto& m : weapon_model::kModels)
        if (m.title == title && !std::strcmp(m.name, name))
            return &m;
    return nullptr;
}
static uint64_t IdOf(GameTitle title, const char* name)
{
    const auto* m = ModelOf(title, name);
    return m ? m->identity : 0;
}

struct Expect
{
    const char* name;
    SizeClass size;
    bool scoped, dual;
};

static void CheckTitle(GameTitle title, const Expect* begin, const Expect* end)
{
    for (const Expect* e = begin; e != end; ++e)
    {
        const uint64_t id = IdOf(title, e->name);
        Check(id != 0, e->name);
        Check(WeaponClassFor(title, id) == e->size, e->name);
        Check(IsScoped(title, id) == e->scoped, e->name);
        Check(CanDualWield(title, id) == e->dual, e->name);
    }
}

static bool Near(float a, float b)
{
    return std::fabs(a - b) < 5e-4f;
}
// Kit scope-glass rows (the weapon kit's measured glass) -> aim-frame lens rows. The kit's first-person vertices are already in the
// r_hand frame for CE/H2/H3/ODST (hand origin 0,0,0), and Reach/H4 put the model origin on the right hand, so the origin is 0.
// H3 sniper: centre (0.02958,-0.00719,0.0701), r 0.00861. H3 battle rifle: centre (0.02757,0.00022,0.08207), r 0.00436.
static void KitLensRowTests()
{
    const ScopeLensRow row =
        KitToAimFrame(GameTitle::Halo3, "fp_sniper_rifle", 0.02958f, -0.00719f, 0.0701f, 0, 0, 0, 0.00861f);
    Check(Near(row.right, 0.02192f) && Near(row.up, 0.21367f) && Near(row.forward, 0.09016f),
        "H3 sniper numbers: right/up/forward");
    Check(Near(row.width, 0.05249f), "H3 sniper numbers: width = glass diameter (2 r * 3.048)");
    const KitLensRow* h3sniper = KitLensRowFor(GameTitle::Halo3, "fp_sniper_rifle");
    Check(h3sniper && Near(h3sniper->row.right, row.right) && Near(h3sniper->row.up, row.up) &&
              Near(h3sniper->row.forward, row.forward) && Near(h3sniper->row.width, 0.0472f) &&
              std::strstr(h3sniper->confidence, "high"),
        "generated H3 sniper row equals the conversion; width is the measured glass long side 47 mm, not 2r");
    const ScopeLensRow br =
        KitToAimFrame(GameTitle::Halo3, "fp_battle_rifle", 0.02757f, 0.00022f, 0.08207f, 0, 0, 0, 0.00436f);
    Check(Near(br.right, -0.00067f) && Near(br.up, 0.25014f) && Near(br.forward, 0.08403f) && Near(br.width, 0.02658f),
        "H3 battle rifle numbers (2 r, above the 0.02 floor)");
    Check(KitLensRowFor(GameTitle::Halo3, "fp_battle_rifle") &&
              Near(KitLensRowFor(GameTitle::Halo3, "fp_battle_rifle")->row.width, 0.0266f),
        "H3 battle rifle glass is 27 mm, not floored to 40 mm");
    const KitLensRow* h3br = KitLensRowFor(GameTitle::Halo3, "fp_battle_rifle");
    Check(h3br && Near(h3br->row.up, br.up) && Near(h3br->row.forward, br.forward) &&
              std::strstr(h3br->confidence, "low"),
        "generated H3 battle rifle row (low confidence)");
    Check(KitLensRowFor(GameTitle::Halo3, "fp_beam_rifle") == nullptr &&
              KitLensRowFor(GameTitle::Halo3, "fp_needler") == nullptr &&
              KitLensRowFor(GameTitle::Halo2, "fp_gpmg") == nullptr,
        "rejected (up < 0.05 m) and zoom-0 weapons ship no row");
    const ScopeLensRow reach =
        KitToAimFrame(GameTitle::HaloReach, "sniper_rifle", 0.03215f, -0.00497f, 0.07773f, 0, 0, 0, 0.01171f);
    Check(Near(reach.right, 0.01515f) && Near(reach.up, 0.23692f) && Near(reach.forward, 0.09799f),
        "Reach sniper: right-hand origin");
    const KitLensRow* reachKit = KitLensRowFor(GameTitle::HaloReach, "sniper_rifle");
    Check(reachKit && Near(reachKit->row.up, reach.up) && Near(reachKit->row.right, reach.right) &&
              Near(reachKit->row.forward, reach.forward) && Near(reachKit->row.width, 0.0603f),
        "generated Reach sniper row equals the conversion; width is the glass long side (60 mm), not 2r (71 mm)");
    Check(reachKit && std::strstr(reachKit->confidence, "medium"), "Reach sniper row keeps its confidence");
    Check(reachKit && reachKit->glassAspect > 0.3f && reachKit->glassAspect < 0.6f && h3sniper &&
              h3sniper->glassAspect > 0.4f && h3sniper->glassAspect < 0.6f,
        "S2: sniper glass is a wide rectangle (kit glass_h / glass_w)");
    Check(h3br && h3br->glassAspect > 0.9f, "S2: battle rifle glass is round");
    Check(Near(KitToAimFrame(GameTitle::Halo3, "x", 0, 0, 0, 0, 0, 0, 0.001f).width, 0.02f),
        "tiny glass floors at 0.02 m");
    Check(Near(KitToAimFrame(GameTitle::Halo3, "x", 0, 0, 0, 0, 0, 0, 0.0f).width, 0.085f),
        "marker-only row: default width");
    // Plausibility guard (same limits as the generator): every shipped row is near the hand and above the barrel axis.
    for (const auto& k : kKitLensRows)
        Check(k.row.forward >= -0.10f && k.row.forward <= 0.60f && k.row.up >= 0.05f && k.row.up <= 0.35f &&
                  std::fabs(k.row.right) <= 0.30f && k.row.width >= 0.02f && k.row.width <= 0.25f,
            k.row.model);
    Check(
        sizeof(kKitLensRows) / sizeof(kKitLensRows[0]) >= 24, "the Reach, H4, H3/ODST, H2 and CE kit rows are present");
    // Placement order: a kit row beats the class starting row; weapons without a row keep the class row.
    ScopeLensRow out{};
    bool fromClass = true;
    const KitLensRow* used = nullptr;
    const weapon_model::Model* sniper = nullptr;
    for (const auto& m : weapon_model::kModels)
        if (m.title == GameTitle::HaloReach && !std::strcmp(m.name, "sniper_rifle"))
            sniper = &m;
    Check(sniper && ScopeLensPlacement(sniper, out, fromClass, &used) && !fromClass && used == reachKit,
        "placement uses the kit row for the Reach sniper");
}

// Every Optic (look-through) weapon must be anchorable: it has a kit lens node row, or it is on this list, where the kit
// itself says the model has no scope glass or marker (its scope-glass table rates the glass confidence none). The
// list is exact in both directions: a new row must leave it, a new Optic profile without glass must join it.
// The Visor kind (Reach magnum, automag, CE pistol, ...) has no lens by design and is not checked.
struct NoGlass
{
    GameTitle title;
    const char* model;
};
static const NoGlass kOpticWithoutGlass[] = {
    {GameTitle::Halo2, "fp_covenant_carbine"},
    {GameTitle::Halo3, "fp_rocket_launcher"},
    {GameTitle::Halo3, "fp_spartan_laser"},
    {GameTitle::Halo3, "fp_covenant_carbine"},
    {GameTitle::Halo3ODST, "fp_rocket_launcher"},
    {GameTitle::Halo3ODST, "fp_spartan_laser"},
    {GameTitle::Halo3ODST, "fp_covenant_carbine"},
    {GameTitle::HaloCE, "rocket launcher"},
    {GameTitle::Halo4, "storm_beam_rifle"},
    {GameTitle::Halo4, "storm_rocket_launcher"},
    {GameTitle::HaloReach, "rocket_launcher"},
};
static bool OnNoGlassList(GameTitle title, const char* model)
{
    for (const auto& e : kOpticWithoutGlass)
        if (e.title == title && !std::strcmp(e.model, model))
            return true;
    return false;
}
static void CheckOpticLensRows()
{
    for (const auto& p : weapon_profile::kProfiles)
    {
        if (p.zoomKind != weapon_profile::ZoomKind::Optic)
            continue;
        const bool row = weapon_profile::KitLensNodeFor(p.title, p.model) != nullptr;
        const bool listed = OnNoGlassList(p.title, p.model);
        std::string what = std::string("Optic weapon has a lens row or is listed as glass-less: ") + p.model;
        Check(row != listed, what.c_str());
    }
    for (const auto& e : kOpticWithoutGlass)
    {
        bool profiled = false;
        for (const auto& p : weapon_profile::kProfiles)
            if (p.title == e.title && !std::strcmp(p.model, e.model) && p.zoomKind == weapon_profile::ZoomKind::Optic)
                profiled = true;
        std::string what = std::string("glass-less list entry is an Optic profile: ") + e.model;
        Check(profiled, what.c_str());
    }
    // The magnum is Visor kind and the kit has no scope geometry for it: no row, and it must stay a visor weapon.
    Check(!weapon_profile::KitLensNodeFor(GameTitle::HaloReach, "magnum"), "Reach magnum has no kit lens row");
    for (const auto& p : weapon_profile::kProfiles)
        if (p.title == GameTitle::HaloReach && !std::strcmp(p.model, "magnum"))
            Check(p.zoomKind == weapon_profile::ZoomKind::Visor, "Reach magnum is a Visor weapon");
}

int main()
{
    CheckOpticLensRows();
    KitLensRowTests();
    // Every row names a real catalog model, once.
    for (const Profile& p : kProfiles)
    {
        Check(ModelOf(p.title, p.model) != nullptr, "row names a catalog model in its title");
        int rows = 0;
        for (const Profile& q : kProfiles)
            rows += q.title == p.title && !std::strcmp(q.model, p.model);
        Check(rows == 1, "no duplicate rows");
        Check(p.size != SizeClass::Unknown, "a row claims a size class");
        Check(!p.dualWield ||
                  ((p.title == GameTitle::Halo3 || p.title == GameTitle::Halo2) && p.size == SizeClass::OneHanded),
            "dual-wield only on Halo 2/3 one-handers");
        Check(!p.hasGripOffset && !p.hasGrabZone && !p.hasScopeLens, "shipped rows leave tunables at fallback");
        Check(p.scoped == (p.zoomKind != ZoomKind::None), "scoped derives from zoom kind");
    }

    // Zoom kind: Optic = sight on the model, Visor = zooms without one.
    {
        auto kind = [](GameTitle t, const char* n) { return ZoomKindFor(t, IdOf(t, n)); };
        Check(kind(GameTitle::HaloCE, "pistol") == ZoomKind::Visor &&
                  ZoomKindVerified(GameTitle::HaloCE, IdOf(GameTitle::HaloCE, "pistol")),
            "CE pistol: visor (verified)");
        Check(kind(GameTitle::HaloCE, "sniper rifle") == ZoomKind::Optic, "CE sniper: optic");
        Check(kind(GameTitle::Halo3, "fp_battle_rifle") == ZoomKind::Optic, "H3 BR: optic");
        Check(kind(GameTitle::Halo3, "fp_covenant_carbine") == ZoomKind::Optic, "H3 carbine: optic");
        Check(kind(GameTitle::HaloReach, "dmr") == ZoomKind::Optic, "Reach DMR: optic");
        Check(kind(GameTitle::Halo4, "storm_dmr") == ZoomKind::Optic, "H4 DMR: optic");
        Check(kind(GameTitle::Halo2, "fp_sniper_rifle") == ZoomKind::Optic, "H2 sniper: optic");
        Check(kind(GameTitle::Halo3, "fp_assault_rifle") == ZoomKind::None, "H3 AR: no zoom");
        Check(kind(GameTitle::HaloReach, "magnum") == ZoomKind::Visor &&
                  kind(GameTitle::Halo3ODST, "fp_automag") == ZoomKind::Visor,
            "pistols with a small top sight: visor");
        Check(kind(GameTitle::Halo4, "storm_assault_rifle") == ZoomKind::Visor &&
                  kind(GameTitle::Halo4, "storm_lmg") == ZoomKind::Visor,
            "H4 smart-scope AR/LMG: visor");
        Check(!ZoomKindVerified(GameTitle::Halo4, IdOf(GameTitle::Halo4, "storm_rail_gun")), "H4 railgun unverified");
        Check(ZoomKindFor(GameTitle::Halo4, 1) == ZoomKind::None && ZoomKindVerified(GameTitle::Halo4, 1),
            "fallback: no zoom");
        Check(KnownNoZoom(GameTitle::Halo3, IdOf(GameTitle::Halo3, "fp_plasma_pistol")) &&
                  KnownNoZoom(GameTitle::Halo3, IdOf(GameTitle::Halo3, "fp_assault_rifle")),
            "look-to-scope off: H3 plasma pistol and AR have no zoom");
        Check(!KnownNoZoom(GameTitle::Halo3, IdOf(GameTitle::Halo3, "fp_battle_rifle")) &&
                  !KnownNoZoom(GameTitle::Halo4, 1) &&
                  !KnownNoZoom(GameTitle::Halo4, IdOf(GameTitle::Halo4, "storm_rail_gun")),
            "look-to-scope stays on for optic, unknown and unverified weapons");
        Check(KnownNoZoom(GameTitle::HaloCE, IdOf(GameTitle::HaloCE, "assault rifle")) &&
                  KnownNoZoom(GameTitle::HaloCE, IdOf(GameTitle::HaloCE, "plasma rifle")) &&
                  KnownNoZoom(GameTitle::HaloCE, IdOf(GameTitle::HaloCE, "needler")) &&
                  KnownNoZoom(GameTitle::HaloCE, IdOf(GameTitle::HaloCE, "flamethrower")) &&
                  !KnownNoZoom(GameTitle::HaloCE, IdOf(GameTitle::HaloCE, "pistol")) &&
                  !KnownNoZoom(GameTitle::HaloCE, IdOf(GameTitle::HaloCE, "sniper rifle")) &&
                  !KnownNoZoom(GameTitle::HaloCE, IdOf(GameTitle::HaloCE, "rocket launcher")),
            "CE: AR/plasma rifle/needler/flamethrower never zoom; pistol, sniper and rocket do");
        int optic = 0, visor = 0, unverified = 0;
        for (const Profile& p : kProfiles)
        {
            optic += p.zoomKind == ZoomKind::Optic;
            visor += p.zoomKind == ZoomKind::Visor;
            unverified += !p.zoomVerified;
        }
        Check(optic > 0 && visor > 0 && unverified > 0, "table uses all kinds and marks unverified rows");
        Check(!std::strcmp(ZoomKindName(ZoomKind::Optic), "optic") &&
                  !std::strcmp(ZoomKindName(ZoomKind::Visor), "visor") &&
                  !std::strcmp(ZoomKindName(ZoomKind::None), "none"),
            "kind names");
    }

    // Filled titles are fully covered (except models deliberately left at
    // fallback), and names are unique keys in them.
    for (const auto& m : weapon_model::kModels)
    {
        if (m.title == GameTitle::None)
            continue;
        const char* const unrowedNames[] = {"fp_gpmg", "fp_jackal_shield", "storm_attach_beam", "storm_bishop_beam",
            "storm_assault_carbine", "storm_stasis_rifle", "storm_stasis_pistol"};
        bool unrowed = false;
        for (const char* name : unrowedNames)
            unrowed =
                unrowed || ((m.title == GameTitle::Halo2 || m.title == GameTitle::Halo4) && !std::strcmp(m.name, name));
        Check((&ProfileFor(m.title, m.identity) == &kFallback) == unrowed,
            "every catalog model has a row, except the listed H2/H4 fallbacks");
        Check(ModelOf(m.title, m.name) == &m, "catalog name is unique within its title");
    }

    // Fallback is today's behaviour.
    Check(&ProfileFor(GameTitle::HaloReach, 0) == &kFallback, "identity 0 -> fallback");
    Check(
        &ProfileFor(GameTitle::Halo3, IdOf(GameTitle::HaloReach, "magnum")) == &kFallback, "title is part of the key");
    Check(&ProfileFor(GameTitle::HaloReach, 0x8000000000000001ull) == &kFallback, "unknown live identity -> fallback");
    Check(WeaponClassFor(GameTitle::Halo4, 1) == SizeClass::Unknown && !IsScoped(GameTitle::Halo4, 1) &&
              !CanDualWield(GameTitle::Halo4, 1),
        "fallback: Unknown class, not scoped, not dual");
    const GrabZone stock = GrabZoneFor(GameTitle::Halo2, 1);
    Check(stock.alongMinM == support_grab::kAlongMinimumM && stock.alongMaxM == support_grab::kAlongMaximumM &&
              stock.lateralMaxM == support_grab::kLateralMaximumM,
        "fallback grab zone == shipped constants (bit-exact)");
    Vec3 v{1, 2, 3};
    Check(!GripOffsetFor(GameTitle::Halo2, 1, v) && v.x == 1 && v.y == 2 && v.z == 3,
        "fallback: no grip offset, output untouched");
    Check(!ScopeLensFor(GameTitle::Halo2, 1, v) && v.x == 1 && v.y == 2 && v.z == 3,
        "fallback: no scope lens, output untouched");
    const GrabZone ar = GrabZoneFor(GameTitle::HaloReach, IdOf(GameTitle::HaloReach, "assault_rifle"));
    Check(ar.alongMinM == stock.alongMinM && ar.alongMaxM == stock.alongMaxM && ar.lateralMaxM == stock.lateralMaxM,
        "row without zone uses stock zone");
    Check(stock.CentreM() > 0.43f && stock.CentreM() < 0.45f && stock.HalfLengthM() > 0.35f &&
              stock.HalfLengthM() < 0.37f && stock.RadiusM() == support_grab::kLateralMaximumM,
        "centre/half-length/radius helpers");

    // Set fields come back as authored (test table through the same lookup).
    {
        Profile tuned{};
        tuned.title = GameTitle::HaloReach;
        tuned.model = "dmr";
        tuned.size = SizeClass::Long;
        tuned.hasGripOffset = true;
        tuned.gripOffsetM = {0.01f, -0.02f, 0.03f};
        tuned.hasGrabZone = true;
        tuned.grabZone = {0.12f, 0.40f, 0.05f};
        tuned.hasScopeLens = true;
        tuned.scopeLensM = {0.15f, 0.0f, 0.06f};
        const Profile table[]{tuned};
        const Profile& p = ProfileFor(table, GameTitle::HaloReach, IdOf(GameTitle::HaloReach, "dmr"));
        Check(&p == &table[0], "table-as-input lookup finds the test row");
        Check(&ProfileFor(table, GameTitle::HaloReach, IdOf(GameTitle::HaloReach, "magnum")) == &kFallback,
            "test table: other weapons fall back");
        Vec3 grip{}, lens{};
        Check(GripOffsetOf(p, grip) && grip.x == 0.01f && grip.y == -0.02f && grip.z == 0.03f, "grip offset returned");
        Check(ScopeLensOf(p, lens) && lens.x == 0.15f && lens.y == 0.0f && lens.z == 0.06f, "scope lens returned");
        const GrabZone z = GrabZoneOf(p);
        Check(z.alongMinM == 0.12f && z.alongMaxM == 0.40f && z.lateralMaxM == 0.05f, "grab zone returned");
        Check(z.CentreM() > 0.259f && z.CentreM() < 0.261f && z.HalfLengthM() > 0.139f && z.HalfLengthM() < 0.141f,
            "authored zone centre/half-length");
    }

    using S = SizeClass;
    // clang-format off
    const Expect reach[] = {
        {"magnum", S::OneHanded, true, false},        {"plasma_pistol", S::OneHanded, false, false},
        {"needler", S::OneHanded, false, false},      {"plasma_rifle", S::OneHanded, false, false},
        {"spike_rifle", S::OneHanded, false, false},  {"assault_rifle", S::Long, false, false},
        {"dmr", S::Long, true, false},                {"needle_rifle", S::Long, true, false},
        {"plasma_repeater", S::Long, false, false},   {"shotgun", S::Long, false, false},
        {"sniper_rifle", S::Long, true, false},       {"focus_rifle", S::Long, true, false},
        {"grenade_launcher", S::Long, false, false},  {"concussion_rifle", S::Long, false, false},
        {"plasma_launcher", S::Long, false, false},   {"rocket_launcher", S::Long, true, false},
        {"spartan_laser", S::Long, true, false},      {"flak_cannon", S::Long, false, false},
        {"target_laser", S::Long, true, false},       {"energy_sword", S::Melee, false, false},
        {"gravity_hammer", S::Melee, false, false},   {"fp_flag", S::Objective, false, false},
        {"skull", S::Objective, false, false},        {"assault_bomb", S::Objective, false, false},
        // clang-format on
    };
    CheckTitle(GameTitle::HaloReach, std::begin(reach), std::end(reach));
    Check(std::size(reach) == 24, "every Reach row is asserted");

    const Expect halo3[] = {
        {"fp_magnum", S::OneHanded, false, true}, {"fp_smg", S::OneHanded, false, true},
        {"fp_plasma_rifle", S::OneHanded, false, true}, {"fp_spike_rifle", S::OneHanded, false, true},
        {"fp_excavator", S::OneHanded, false, true}, {"fp_needler", S::OneHanded, false, true},
        {"fp_plasma_pistol", S::OneHanded, false, true}, {"fp_automag", S::OneHanded, true, false},
        // clang-format off
        {"fp_assault_rifle", S::Long, false, false},    {"fp_battle_rifle", S::Long, true, false},
        {"fp_covenant_carbine", S::Long, true, false},  {"fp_sniper_rifle", S::Long, true, false},
        {"fp_beam_rifle", S::Long, true, false},        {"fp_smg_silenced", S::OneHanded, true, false},
        {"fp_rocket_launcher", S::Long, true, false},   {"fp_spartan_laser", S::Long, true, false},
        {"fp_shotgun", S::Long, false, false},          {"fp_brute_shot", S::Long, false, false},
        {"fp_sentinel_gun", S::Long, false, false},     {"flak_cannon", S::Long, true, false},
        {"fp_flak_cannon", S::Long, true, false},      {"fp_energy_blade", S::Melee, false, false},
        {"fp_gravity_hammer", S::Melee, false, false},  {"fp_flag", S::Objective, false, false},
        {"fp_ball", S::Objective, false, false},        {"fp_assault_bomb", S::Objective, false, false},
        // clang-format on
    };
    CheckTitle(GameTitle::Halo3, std::begin(halo3), std::end(halo3));
    Check(std::size(halo3) == 26, "every Halo 3 row is asserted");

    // ODST mirrors Halo 3 without dual wielding, plus the brute plasma rifle.
    Expect odst[27]{};
    for (size_t i = 0; i < std::size(halo3); ++i)
        odst[i] = {halo3[i].name, halo3[i].size, halo3[i].scoped, false};
    odst[26] = {"fp_plasma_rifle_red", S::OneHanded, false, false};
    CheckTitle(GameTitle::Halo3ODST, std::begin(odst), std::end(odst));

    // clang-format off
    const Expect halo2[] = {
        {"fp_magnum", S::OneHanded, false, true},             {"fp_smg", S::OneHanded, false, true},
        {"fp_smg_silenced", S::OneHanded, false, true},      {"fp_plasma_rifle", S::OneHanded, false, true},
        // clang-format on
        {"fp_brute_plasma_rifle", S::OneHanded, false, true}, {"fp_plasma_pistol", S::OneHanded, false, true},
        // clang-format off
        {"fp_needler", S::OneHanded, false, true},           {"fp_battle_rifle", S::Long, true, false},
        {"fp_covenant_carbine", S::Long, true, false},       {"fp_beam_rifle", S::Long, true, false},
        {"fp_sniper_rifle", S::Long, true, false},           {"fp_rocket_launcher", S::Long, true, false},
        {"fp_shotgun", S::Long, false, false},               {"fp_brute_shot", S::Long, false, false},
        {"flak_cannon", S::Long, true, false},              {"fp_energy_blade", S::Melee, false, false},
        {"gravity_hammer", S::Melee, false, false},          {"fp_flag", S::Objective, false, false},
        {"fp_ball", S::Objective, false, false},             {"fp_assault_bomb", S::Objective, false, false},
        {"fp_gpmg", S::Unknown, false, false},               {"fp_jackal_shield", S::Unknown, false, false},
        // clang-format on
    };
    CheckTitle(GameTitle::Halo2, std::begin(halo2), std::end(halo2));
    Check(std::size(halo2) == 22, "every Halo 2 model is asserted");

    // clang-format off
    const Expect ce[] = {
        {"pistol", S::OneHanded, true, false},        {"plasma pistol", S::OneHanded, false, false},
        {"needler", S::OneHanded, false, false},      {"plasma rifle", S::OneHanded, false, false},
        {"assault rifle", S::Long, false, false},     {"shotgun", S::Long, false, false},
        {"sniper rifle", S::Long, true, false},       {"rocket launcher", S::Long, true, false},
        {"flamethrower", S::Long, false, false},      {"plasma_cannon", S::Long, true, false},
        {"flag", S::Objective, false, false},         {"ball", S::Objective, false, false},
        // clang-format on
    };
    CheckTitle(GameTitle::HaloCE, std::begin(ce), std::end(ce));
    Check(std::size(ce) == 12, "every Halo CE row is asserted");

    const Expect halo4[] = {
        {"storm_magnum", S::OneHanded, true, false},
        {"storm_burst_pistol", S::OneHanded, true, false},
        {"storm_plasma_pistol", S::OneHanded, false, false},
        {"plasma_pistol", S::OneHanded, false, false},
        {"storm_needler", S::OneHanded, true, false},
        {"storm_sticky_detonator", S::OneHanded, false, false},
        {"storm_assault_rifle", S::Long, true, false},
        {"storm_br", S::Long, true, false},
        {"storm_dmr", S::Long, true, false},
        {"storm_covenant_carbine", S::Long, true, false},
        {"storm_forerunner_rifle", S::Long, true, false},
        {"storm_forerunner_smg", S::Long, true, false},
        {"storm_lmg", S::Long, true, false},
        {"storm_shotgun", S::Long, false, false},
        {"storm_spread_gun", S::Long, false, false},
        {"storm_sniper_rifle", S::Long, true, false},
        {"storm_beam_rifle", S::Long, true, false},
        {"storm_forerunner_sniper_rifle", S::Long, true, false},
        {"storm_rocket_launcher", S::Long, true, false},
        {"storm_spartan_laser", S::Long, true, false},
        {"storm_rail_gun", S::Long, false, false},
        {"storm_fuel_rod_cannon", S::Long, true, false},
        {"storm_forerunner_incineration_launcher", S::Long, false, false},
        {"storm_concussion_rifle", S::Long, true, false},
        {"storm_target_laser", S::Long, true, false},
        {"target_laser", S::Long, true, false},
        {"energy_sword", S::Melee, false, false},
        {"storm_energy_sword", S::Melee, false, false},
        {"storm_flood_sword", S::Melee, false, false},
        {"storm_gravity_hammer", S::Melee, false, false},
        {"fp_flag", S::Objective, false, false},
        {"storm_skull", S::Objective, false, false},
        {"blam_bomb", S::Objective, false, false},
        {"storm_blam_bomb", S::Objective, false, false},
        {"storm_attach_beam", S::Unknown, false, false},
        {"storm_bishop_beam", S::Unknown, false, false},
        {"storm_assault_carbine", S::Unknown, false, false},
        {"storm_stasis_rifle", S::Unknown, false, false},
        {"storm_stasis_pistol", S::Unknown, false, false},
    };
    CheckTitle(GameTitle::Halo4, std::begin(halo4), std::end(halo4));
    Check(std::size(halo4) == 39, "every Halo 4 model is asserted");

    // F1 Weapon tuning: grab zone override and export line.
    {
        const GrabZone stockZone{};
        Check(!GrabOverrideActive(0.44f, 0.36f, 0.0f) && !GrabOverrideActive(0, 0.1f, 0.1f), "any zero = not tuned");
        const GrabZone same = GrabZoneOverride(stockZone, 0.44f, 0.0f, 0.09f);
        Check(same.alongMinM == stockZone.alongMinM && same.alongMaxM == stockZone.alongMaxM,
            "inactive override keeps the profile zone");
        const GrabZone tuned = GrabZoneOverride(stockZone, 0.30f, 0.10f, 0.05f);
        Check(std::fabs(tuned.alongMinM - 0.20f) < 1e-5f && std::fabs(tuned.alongMaxM - 0.40f) < 1e-5f &&
                  tuned.lateralMaxM == 0.05f,
            "centre/half/radius -> min/max/lateral");
        const GrabZone clipped = GrabZoneOverride(stockZone, 0.05f, 0.20f, 1.0f);
        Check(clipped.alongMinM == 0.0f && clipped.lateralMaxM == kGrabRadiusMaxM, "min floored at 0, radius clamped");
        const GrabZone wide = GrabZoneOverride(stockZone, 5.0f, 5.0f, 0.05f);
        Check(std::fabs(wide.alongMaxM - (kGrabCentreMaxM + kGrabHalfLengthMaxM)) < 1e-5f,
            "centre and half length clamped");
        weapon_tuning::Row row{};
        row.title = "Halo3";
        row.weapon = "fp_battle_rifle";
        row.gripForward = 0.02f;
        row.zoneTuned = true;
        row.alongMin = 0.2f;
        row.alongMax = 0.4f;
        row.lateralMax = 0.05f;
        const std::string line = weapon_tuning::Format(row);
        Check(line.find("{GameTitle::Halo3, \"fp_battle_rifle\"}") != std::string::npos &&
                  line.find("gripOffsetM={0.020f, 0.000f, 0.000f}") != std::string::npos &&
                  line.find("grabZone={0.200f, 0.400f, 0.050f}") != std::string::npos,
            "export row format");
        row.zoneTuned = false;
        Check(weapon_tuning::Format(row).find("hasGrabZone") == std::string::npos, "untuned zone not exported");
        Check(weapon_tuning::Format(row).find("kScopeLensRows") == std::string::npos, "no lens row unless asked");
        row.lensExport = true;
        row.lensRight = -0.01f;
        row.lensUp = 0.17f;
        row.lensForward = 0.06f;
        row.lensWidth = 0.09f;
        Check(weapon_tuning::Format(row).find(
                  "{GameTitle::Halo3, \"fp_battle_rifle\", -0.010f, 0.170f, 0.060f, 0.090f},") != std::string::npos,
            "the export carries a paste-ready scope lens row");
        Check(weapon_tuning::ExportPath(L"C:\\Games\\Halo_MCC_VR\\halomccvr.cfg") ==
                      std::filesystem::path(L"C:\\Games\\Halo_MCC_VR\\HaloMCCVR-weapon-tuning.txt") &&
                  weapon_tuning::ExportPath(L"D:/mod/halomccvr.cfg") ==
                      std::filesystem::path(L"D:/mod/HaloMCCVR-weapon-tuning.txt"),
            "export lands beside the config (either separator)");
        Check(weapon_tuning::ExportPath(L"").empty() && weapon_tuning::ExportPath(L"halomccvr.cfg").empty(),
            "no config path or no folder: refuse");
        Check(std::string(weapon_tuning::TitleEnumName(static_cast<int>(GameTitle::HaloReach))) == "HaloReach" &&
                  std::string(weapon_tuning::TitleEnumName(static_cast<int>(GameTitle::Halo2))) == "Halo2",
            "title names match GameTitle");
    }
    {
        // Per title: which guns get live glass (Optic + kit lens node), and the kit glass shape.
        auto id = [](GameTitle t, const char* name) -> uint64_t
        {
            for (const auto& m : weapon_model::kModels)
                if (m.title == t && !std::strcmp(m.name, name))
                    return m.identity;
            return 0;
        };
        for (const GameTitle t : {GameTitle::Halo3, GameTitle::Halo3ODST})
        {
            for (const char* glass : {"fp_sniper_rifle", "fp_battle_rifle", "fp_beam_rifle", "fp_smg_silenced"})
                Check(id(t, glass) && LiveGlassGun(t, id(t, glass)), glass);
            for (const char* other : {"fp_rocket_launcher", "fp_spartan_laser", "fp_covenant_carbine", "fp_automag",
                     "fp_flak_cannon", "fp_assault_rifle"})
                Check(!id(t, other) || !LiveGlassGun(t, id(t, other)), other);
            const KitLensRow* sniper = KitLensRowFor(t, "fp_sniper_rifle");
            const KitLensRow* br = KitLensRowFor(t, "fp_battle_rifle");
            Check(sniper && sniper->glassAspect > 0.4f && sniper->glassAspect < 0.6f && br && br->glassAspect > 0.9f,
                "H3/ODST: sniper glass is a wide rectangle, BR glass round");
        }
        Check(id(GameTitle::Halo3, "fp_sniper_rifle") && id(GameTitle::Halo3ODST, "fp_sniper_rifle"),
            "H3/ODST sniper models are in the catalog");
        Check(LiveGlassGun(GameTitle::HaloReach, id(GameTitle::HaloReach, "sniper_rifle")) &&
                  LiveGlassGun(GameTitle::HaloReach, id(GameTitle::HaloReach, "dmr")) &&
                  !LiveGlassGun(GameTitle::HaloReach, id(GameTitle::HaloReach, "magnum")),
            "Reach: sniper and DMR live glass, magnum keeps helmet zoom");
        Check(!LiveGlassGun(GameTitle::Halo3, 0), "no weapon: no live glass");
        for (const char* glass : {"fp_battle_rifle", "fp_beam_rifle", "fp_sniper_rifle"})
            Check(id(GameTitle::Halo2, glass) && LiveGlassGun(GameTitle::Halo2, id(GameTitle::Halo2, glass)), glass);
        for (const char* other : {"fp_covenant_carbine", "fp_rocket_launcher", "flak_cannon"})
            Check(!id(GameTitle::Halo2, other) || !LiveGlassGun(GameTitle::Halo2, id(GameTitle::Halo2, other)), other);
        for (const char* glass : {"storm_br", "storm_dmr", "storm_sniper_rifle", "storm_covenant_carbine",
                 "storm_forerunner_rifle", "storm_forerunner_sniper_rifle", "storm_spartan_laser"})
        {
            Check(id(GameTitle::Halo4, glass) && LiveGlassGun(GameTitle::Halo4, id(GameTitle::Halo4, glass)), glass);
            const KitLensRow* kit = KitLensRowFor(GameTitle::Halo4, glass);
            const bool markerOnly = !strcmp(glass, "storm_covenant_carbine") ||
                                    !strcmp(glass, "storm_forerunner_rifle") ||
                                    !strcmp(glass, "storm_forerunner_sniper_rifle");
            if (markerOnly)
                Check(!kit || kit->glassAspect == 0, "H4 marker-only glass stays a circle (no glass part to measure)");
            else
                Check(kit && kit->glassAspect > 0.0f && kit->glassAspect <= 1.0f && kit->row.width >= 0.02f &&
                          kit->row.width < 0.10f,
                    "H4 measured glass: aspect in (0,1], width from glass_w");
        }
        for (const char* other :
            {"storm_beam_rifle", "storm_rocket_launcher", "storm_magnum", "storm_assault_rifle", "storm_lmg"})
            Check(!id(GameTitle::Halo4, other) || !LiveGlassGun(GameTitle::Halo4, id(GameTitle::Halo4, other)), other);
        const KitLensRow* h2sniper = KitLensRowFor(GameTitle::Halo2, "fp_sniper_rifle");
        Check(
            h2sniper && h2sniper->glassAspect > 0.5f && h2sniper->glassAspect < 0.7f, "H2 sniper glass is a rectangle");
    }
    if (g_failures)
    {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("weapon_profile_tests: all passed\n");
    return 0;
}
