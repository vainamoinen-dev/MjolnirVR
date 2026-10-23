// Per-weapon profile table: size class, zoom kind, lens, dual wield, grip offset and grab zone per game and gun.
// Used by config.cpp, lens_anchor.h, weapon_interaction_logic.h and src/dll/core (menu.cpp, vr.cpp);
// tested by tests/weapon_profile_tests.cpp. Pure data and lookups: no globals, no locks, no logging.
#pragma once
#include "math_types.h"
// Per-weapon profile table: the per-game facts features need about a held gun
// (size class for holsters, scope flag/lens, dual wield, grip offset, two-hand
// grab zone). Rows name the official model in the existing weapon_model
// catalog; a lookup resolves the live identity through weapon_model::Find and
// matches the row by title + catalog name, so no identity is stored twice.
//
// Any weapon without a row, and any field a row leaves unset, resolves to the
// fallback, which is exactly today's behaviour:
//   - size class Unknown (caller keeps its current, class-blind behaviour);
//   - not scoped, not dual-wieldable, no lens;
//   - no authored grip offset (caller keeps the global gun_* config and the
//     user's per-weapon weapon_alignment_* cfg profiles);
//   - grab zone = support_grab::kAlongMinimumM/kAlongMaximumM/kLateralMaximumM.
// A field changes behaviour only once a row sets it with evidence.
//
// Pure data and lookups: no globals, no locks, no logging.
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "runtime_types.h"
#include "support_grab_logic.h"
#include "weapon_model_catalog.h"

namespace weapon_profile
{
    enum class SizeClass : uint8_t
    {
        Unknown = 0, // fallback: no claim
        OneHanded,   // pistol-sized carry; hip holster
        Long,        // two-handed; back holster
        Melee,       // sword, hammer; not a gun
        Objective,   // flag, skull, bomb; never holstered
    };

    // How a zoomable weapon should zoom in VR.
    //  Optic: the model has a real sight/scope -> look-through lens.
    //  Visor: zooms but has no sight on the model (e.g. Halo CE pistol) -> HUD
    //         visor zoom.
    //  None:  no native zoom.
    enum class ZoomKind : uint8_t
    {
        None = 0,
        Optic,
        Visor
    };

    // Table shorthand: Q = whether the model carries a sight is uncertain; check
    // it in-game or in the tags before relying on it (Profile::zoomVerified
    // false). Unmarked rows are well-known sights (BR, DMR, sniper scopes).
    // Rule: a pistol with only a small top sight is Visor (the CE pistol is the
    // reference); so are Halo 4's smart-scope AR/LMG (HUD zoom, no optic).
    enum class ZoomRow : uint8_t
    {
        No,
        Optic,
        Visor,
        NoQ,
        OpticQ,
        VisorQ
    };
    constexpr ZoomKind ZoomKindOfRow(ZoomRow z) noexcept
    {
        return z == ZoomRow::Optic || z == ZoomRow::OpticQ   ? ZoomKind::Optic
               : z == ZoomRow::Visor || z == ZoomRow::VisorQ ? ZoomKind::Visor
                                                             : ZoomKind::None;
    }
    constexpr bool ZoomRowVerified(ZoomRow z) noexcept
    {
        return z != ZoomRow::NoQ && z != ZoomRow::OpticQ && z != ZoomRow::VisorQ;
    }

    using Vec3 = mvr_math::Vec3;

    // Two-hand grab zone along the barrel, in the primary controller's frame
    // (same meaning as support_grab::EvaluateSupportGrabZone's thresholds).
    struct GrabZone
    {
        float alongMinM = support_grab::kAlongMinimumM;
        float alongMaxM = support_grab::kAlongMaximumM;
        float lateralMaxM = support_grab::kLateralMaximumM;
        constexpr float CentreM() const noexcept { return (alongMinM + alongMaxM) * 0.5f; }
        constexpr float HalfLengthM() const noexcept { return (alongMaxM - alongMinM) * 0.5f; }
        constexpr float RadiusM() const noexcept { return lateralMaxM; }
    };

    // Per-weapon default for the zoom lens placement, in the aim frame (the same
    // metres the scope_screen_* settings use): where the lens quad sits on the gun's
    // scope glass and how wide it is. A weapon without a row (or with its own saved
    // tuning) uses the global scope_screen_* values. Rows are captured from F1
    // Weapon tuning exports per weapon; none ship until captured.
    struct ScopeLensRow
    {
        GameTitle title;
        const char* model; // weapon_model::Model::name in this title
        float right, up, forward, width;
    };
    inline constexpr ScopeLensRow kScopeLensRows[] = {
        // Hand-captured rows (F1 Weapon tuning exports) go here and win over the kit rows below.
        {GameTitle::None, "", 0.0f, 0.0f, 0.0f, 0.0f}, // sentinel: replace with captured rows
    };

    // Kit-measured scope glass, converted to the aim frame by
    // tools/kits/gen_scope_lens_rows.py. `row` is what the lens quad uses; the rest is the source
    // measurement (model space, world units) kept for the "Scope lens: ..." log line.
    struct KitLensRow
    {
        ScopeLensRow row;
        const char* method;     // how the kit measured it (glass disc / marker only / ...)
        const char* confidence; // high | medium | low
        float cx, cy, cz;       // glass centre, first-person render model frame (x fwd, y left, z up)
        float radius;           // 0 for marker-only rows
        float glassAspect;      // glass short / long side (kit glass_h, glass_w); 0 = unmeasured
    };
    inline constexpr float kMetresPerWorldUnit = 3.048f;
    inline constexpr float kKitMinWidthM = 0.02f; // = F1 / config minimum of scope_screen_width_m
    inline constexpr float kKitDefaultWidthM = 0.085f;
    // Kit model frame -> aim frame metres. The origin is the right_hand marker (Reach/H4) or the root node
    // (CE/H2/H3/ODST). Width = 2 r, floored at kKitMinWidthM; radius <= 0 -> the rifle default. This is the circumscribed-radius rule only:
    // the generator uses the measured glass_w instead when the csv has one (H2/H3/ODST/Reach) and keeps a 0.04 m floor for unmeasured rows
    // (CE, H4), so generated widths can differ from this function; tests pin the generated values directly.
    constexpr ScopeLensRow KitToAimFrame(GameTitle title, const char* model, float cx, float cy, float cz, float ox,
        float oy, float oz, float radius) noexcept
    {
        const float diameter = 2.0f * radius * kMetresPerWorldUnit;
        return ScopeLensRow{title, model, -(cy - oy) * kMetresPerWorldUnit, (cz - oz) * kMetresPerWorldUnit,
            (cx - ox) * kMetresPerWorldUnit,
            radius <= 0.0f ? kKitDefaultWidthM : (diameter < kKitMinWidthM ? kKitMinWidthM : diameter)};
    }
    // Glass centre in the weapon's root node (node 0) frame; the runtime anchors the lens to the rendered node.
    struct LensNodeRow
    {
        GameTitle title;
        const char* model;
        float x, y, z;
    };
#include "scope_lens_rows.generated.h"
    inline const LensNodeRow* KitLensNodeFor(GameTitle title, const char* model) noexcept
    {
        if (!model)
            return nullptr;
        for (const auto& k : kKitLensNodes)
            if (k.title == title && k.model && !std::strcmp(k.model, model))
                return &k;
        return nullptr;
    }
    inline const KitLensRow* KitLensRowFor(GameTitle title, const char* model) noexcept
    {
        if (!model)
            return nullptr;
        for (const auto& k : kKitLensRows)
            if (k.row.title == title && k.row.model && !std::strcmp(k.row.model, model))
                return &k;
        return nullptr;
    }
    inline const ScopeLensRow* ScopeLensRowIn(
        const ScopeLensRow* rows, size_t count, GameTitle title, const char* model) noexcept
    {
        if (!rows || !model)
            return nullptr;
        for (size_t i = 0; i < count; ++i)
            if (rows[i].title == title && rows[i].title != GameTitle::None && rows[i].model &&
                !std::strcmp(rows[i].model, model))
                return &rows[i];
        return nullptr;
    }
    inline const ScopeLensRow* ScopeLensRowFor(GameTitle title, const char* model) noexcept
    {
        return ScopeLensRowIn(kScopeLensRows, sizeof(kScopeLensRows) / sizeof(kScopeLensRows[0]), title, model);
    }

    // Per-class starting placements for every Optic weapon (fixed-placement fixed
    // offsets per weapon type, not per-model markers): a sniper-class tube, a rifle
    // scope and the heavy launcher/laser sights. Same aim-frame metres as the rows.
    // HEADSET-UNVERIFIED starting points: F1 Weapon tuning shows a live marker and
    // saves the corrected placement per weapon, which then becomes an explicit row.
    enum class LensClass : uint8_t
    {
        Rifle,
        Sniper,
        Heavy
    };
    inline constexpr LensClass LensClassOf(const char* model) noexcept
    {
        if (!model)
            return LensClass::Rifle;
        const auto has = [model](const char* word)
        {
            for (const char* p = model; *p; ++p)
            {
                const char* a = p;
                const char* b = word;
                while (*a && *b && *a == *b)
                {
                    ++a;
                    ++b;
                }
                if (!*b)
                    return true;
            }
            return false;
        };
        return has("sniper") || has("beam") ? LensClass::Sniper
               : has("rocket") || has("laser") || has("spartan") || has("designator") || has("locator")
                   ? LensClass::Heavy
                   : LensClass::Rifle;
    }
    inline constexpr ScopeLensRow ClassRow(GameTitle title, const char* model) noexcept
    {
        const LensClass c = LensClassOf(model);
        return c == LensClass::Sniper  ? ScopeLensRow{title, model, 0.0f, 0.150f, 0.05f, 0.100f}
               : c == LensClass::Heavy ? ScopeLensRow{title, model, 0.0f, 0.180f, 0.05f, 0.120f}
                                       : ScopeLensRow{title, model, 0.0f, 0.150f, 0.05f, 0.085f};
    }

    struct Profile
    {
        GameTitle title = GameTitle::None;
        const char* model = ""; // weapon_model::Model::name in this title
        SizeClass size = SizeClass::Unknown;
        bool scoped = false; // has a native zoom (derived: zoomKind != None)
        ZoomKind zoomKind = ZoomKind::None;
        bool zoomVerified = true; // false: zoomKind not yet checked on the model
        bool dualWield = false;   // the game lets this weapon be dual-wielded
        // Authored grip offset (metres; same axes as gun_forward_m/right_m/up_m).
        bool hasGripOffset = false;
        Vec3 gripOffsetM{};
        bool hasGrabZone = false;
        GrabZone grabZone{};
        // Lens centre in the held model's bind-local frame (as Model::receiverAnchor).
        bool hasScopeLens = false;
        Vec3 scopeLensM{};
    };

    inline constexpr Profile kFallback{};

    constexpr Profile Row(GameTitle title, const char* model, SizeClass size, ZoomRow zoom, bool dualWield) noexcept
    {
        Profile p{};
        p.title = title;
        p.model = model;
        p.size = size;
        p.dualWield = dualWield;
        p.zoomKind = ZoomKindOfRow(zoom);
        p.zoomVerified = ZoomRowVerified(zoom);
        p.scoped = p.zoomKind != ZoomKind::None;
        return p;
    }

#define WP_H3(model, size, zoom, dual) Row(GameTitle::Halo3, model, SizeClass::size, ZoomRow::zoom, dual)
#define WP_ODST(model, size, zoom) Row(GameTitle::Halo3ODST, model, SizeClass::size, ZoomRow::zoom, false)
#define WP_REACH(model, size, zoom) Row(GameTitle::HaloReach, model, SizeClass::size, ZoomRow::zoom, false)
#define WP_H2(model, size, zoom, dual) Row(GameTitle::Halo2, model, SizeClass::size, ZoomRow::zoom, dual)
#define WP_CE(model, size, zoom) Row(GameTitle::HaloCE, model, SizeClass::size, ZoomRow::zoom, false)
#define WP_H4(model, size, zoom) Row(GameTitle::Halo4, model, SizeClass::size, ZoomRow::zoom, false)

    // Size classes, zoom and dual-wield flags follow each game's own handling
    // (one-handed vs two-handed carry, native zoom). Zoom kind follows the model:
    // Optic when it carries a sight/scope, Visor when it zooms without one; a Q
    // row is unverified. Offsets/zones/lenses stay at
    // fallback until a headset tune or tag evidence sets them.
    inline constexpr Profile kProfiles[]{
        // Halo 3: one-handers dual-wield. The catalog also carries ODST's automag
        // and silenced SMG for this title; they keep ODST's handling (no dual; the
        // silenced SMG carries on the hip like Halo 3's SMG).
        WP_H3("fp_rocket_launcher", Long, Optic, false),
        WP_H3("fp_spartan_laser", Long, OpticQ, false),
        WP_H3("fp_battle_rifle", Long, Optic, false),
        WP_H3("fp_beam_rifle", Long, Optic, false),
        WP_H3("fp_plasma_rifle", OneHanded, No, true),
        WP_H3("fp_smg_silenced", OneHanded, Optic, false),
        WP_H3("fp_needler", OneHanded, No, true),
        WP_H3("fp_sniper_rifle", Long, Optic, false),
        WP_H3("fp_assault_bomb", Objective, No, false),
        WP_H3("fp_automag", OneHanded, Visor, false),
        WP_H3("fp_sentinel_gun", Long, No, false),
        WP_H3("flak_cannon", Long, Visor, false), // kit: 1 zoom level (2x), no sight on the model
        WP_H3("fp_smg", OneHanded, No, true),
        WP_H3("fp_plasma_pistol", OneHanded, No, true),
        WP_H3("fp_assault_rifle", Long, No, false),
        WP_H3("fp_magnum", OneHanded, No, true),
        WP_H3("fp_flag", Objective, No, false),
        WP_H3("fp_brute_shot", Long, No, false),
        WP_H3("fp_covenant_carbine", Long, Optic, false),
        WP_H3("fp_excavator", OneHanded, No, true),
        WP_H3("fp_shotgun", Long, No, false),
        WP_H3("fp_energy_blade", Melee, No, false),
        WP_H3("fp_ball", Objective, No, false),
        WP_H3("fp_spike_rifle", OneHanded, No, true),
        WP_H3("fp_gravity_hammer", Melee, No, false),
        WP_H3("fp_flak_cannon", Long, Visor, false),
        // ODST: no dual wielding; same classes and zoom as Halo 3's weapons.
        WP_ODST("fp_rocket_launcher", Long, Optic),
        WP_ODST("fp_spartan_laser", Long, OpticQ),
        WP_ODST("fp_battle_rifle", Long, Optic),
        WP_ODST("fp_beam_rifle", Long, Optic),
        WP_ODST("fp_plasma_rifle", OneHanded, No),
        WP_ODST("fp_smg_silenced", OneHanded, Optic),
        WP_ODST("fp_needler", OneHanded, No),
        WP_ODST("fp_sniper_rifle", Long, Optic),
        WP_ODST("fp_assault_bomb", Objective, No),
        WP_ODST("fp_automag", OneHanded, Visor),
        WP_ODST("fp_sentinel_gun", Long, No),
        WP_ODST("flak_cannon", Long, Visor),
        WP_ODST("fp_smg", OneHanded, No),
        WP_ODST("fp_plasma_pistol", OneHanded, No),
        WP_ODST("fp_assault_rifle", Long, No),
        WP_ODST("fp_magnum", OneHanded, No),
        WP_ODST("fp_flag", Objective, No),
        WP_ODST("fp_brute_shot", Long, No),
        WP_ODST("fp_covenant_carbine", Long, Optic),
        WP_ODST("fp_excavator", OneHanded, No),
        WP_ODST("fp_shotgun", Long, No),
        WP_ODST("fp_energy_blade", Melee, No),
        WP_ODST("fp_ball", Objective, No),
        WP_ODST("fp_spike_rifle", OneHanded, No),
        WP_ODST("fp_plasma_rifle_red", OneHanded, No),
        WP_ODST("fp_gravity_hammer", Melee, No),
        WP_ODST("fp_flak_cannon", Long, Visor),
        // Halo 2: one rowset for both renderers. The live identity is read from the
        // classic simulation's tags (Halo2ObserveReloadModel), which Anniversary
        // graphics do not replace. fp_gpmg and fp_jackal_shield have no row: no
        // evidence of how the player carries them, so they keep the fallback.
        WP_H2("fp_magnum", OneHanded, No, true),
        WP_H2("fp_smg", OneHanded, No, true),
        WP_H2("fp_smg_silenced", OneHanded, No, true),
        WP_H2("fp_plasma_rifle", OneHanded, No, true),
        WP_H2("fp_brute_plasma_rifle", OneHanded, No, true),
        WP_H2("fp_plasma_pistol", OneHanded, No, true),
        WP_H2("fp_needler", OneHanded, No, true),
        WP_H2("fp_battle_rifle", Long, Optic, false),
        WP_H2("fp_covenant_carbine", Long, Optic, false),
        WP_H2("fp_beam_rifle", Long, Optic, false),
        WP_H2("fp_sniper_rifle", Long, Optic, false),
        WP_H2("fp_rocket_launcher", Long, Visor, false), // kit: no sight geometry, HUD zoom only
        WP_H2("fp_shotgun", Long, No, false),
        WP_H2("fp_brute_shot", Long, No, false),
        WP_H2("flak_cannon", Long, Visor, false), // fuel rod gun: 2x, no sight on the model
        WP_H2("fp_energy_blade", Melee, No, false),
        WP_H2("gravity_hammer", Melee, No, false),
        WP_H2("fp_flag", Objective, No, false),
        WP_H2("fp_ball", Objective, No, false),
        WP_H2("fp_assault_bomb", Objective, No, false),
        // Halo CE: no dual wielding.
        WP_CE("pistol", OneHanded, Visor),
        WP_CE("plasma pistol", OneHanded, No),
        WP_CE("needler", OneHanded, No),
        WP_CE("plasma rifle", OneHanded, No),
        WP_CE("assault rifle", Long, No),
        WP_CE("shotgun", Long, No),
        WP_CE("sniper rifle", Long, Optic),
        WP_CE("rocket launcher", Long, OpticQ),
        WP_CE("flamethrower", Long, No),
        WP_CE("plasma_cannon", Long, VisorQ), // fuel rod gun
        WP_CE("flag", Objective, No),
        WP_CE("ball", Objective, No),
        // Halo 4: no dual wielding. storm_attach_beam, storm_bishop_beam,
        // storm_assault_carbine, storm_stasis_rifle and storm_stasis_pistol stay at
        // fallback (which weapon each is, and its carry, is not established).
        WP_H4("storm_magnum", OneHanded, VisorQ),
        WP_H4("storm_burst_pistol", OneHanded, VisorQ),
        WP_H4("storm_plasma_pistol", OneHanded, No),
        WP_H4("plasma_pistol", OneHanded, No),
        WP_H4("storm_needler", OneHanded, VisorQ),
        WP_H4("storm_sticky_detonator", OneHanded, No),
        WP_H4("storm_assault_rifle", Long, Visor),
        WP_H4("storm_br", Long, Optic),
        WP_H4("storm_dmr", Long, Optic),
        WP_H4("storm_covenant_carbine", Long, Optic),
        WP_H4("storm_forerunner_rifle", Long, OpticQ),
        WP_H4("storm_forerunner_smg", Long, VisorQ),
        WP_H4("storm_lmg", Long, Visor),
        WP_H4("storm_shotgun", Long, No),
        WP_H4("storm_spread_gun", Long, No),
        WP_H4("storm_sniper_rifle", Long, Optic),
        WP_H4("storm_beam_rifle", Long, Optic),
        WP_H4("storm_forerunner_sniper_rifle", Long, OpticQ),
        WP_H4("storm_rocket_launcher", Long, Optic),
        WP_H4("storm_spartan_laser", Long, OpticQ),
        WP_H4("storm_rail_gun", Long, NoQ),
        WP_H4("storm_fuel_rod_cannon", Long, VisorQ),
        WP_H4("storm_forerunner_incineration_launcher", Long, No),
        WP_H4("storm_concussion_rifle", Long, VisorQ),
        WP_H4("storm_target_laser", Long, OpticQ),
        WP_H4("target_laser", Long, OpticQ),
        WP_H4("energy_sword", Melee, No),
        WP_H4("storm_energy_sword", Melee, No),
        WP_H4("storm_flood_sword", Melee, No),
        WP_H4("storm_gravity_hammer", Melee, No),
        WP_H4("fp_flag", Objective, No),
        WP_H4("storm_skull", Objective, No),
        WP_H4("blam_bomb", Objective, No),
        WP_H4("storm_blam_bomb", Objective, No),
        // Reach: no dual wielding.
        WP_REACH("magnum", OneHanded, Visor),
        WP_REACH("plasma_pistol", OneHanded, No),
        WP_REACH("needler", OneHanded, No),
        WP_REACH("plasma_rifle", OneHanded, No),
        WP_REACH("spike_rifle", OneHanded, No),
        WP_REACH("assault_rifle", Long, No),
        WP_REACH("dmr", Long, Optic),
        WP_REACH("needle_rifle", Long, Optic),
        WP_REACH("plasma_repeater", Long, No),
        WP_REACH("shotgun", Long, No),
        WP_REACH("sniper_rifle", Long, Optic),
        WP_REACH("focus_rifle", Long, OpticQ),
        WP_REACH("grenade_launcher", Long, No),
        WP_REACH("concussion_rifle", Long, No),
        WP_REACH("plasma_launcher", Long, No),
        WP_REACH("rocket_launcher", Long, Optic),
        WP_REACH("spartan_laser", Long, OpticQ),
        WP_REACH("flak_cannon", Long, No), // fuel rod gun
        WP_REACH("target_laser", Long, OpticQ),
        WP_REACH("energy_sword", Melee, No),
        WP_REACH("gravity_hammer", Melee, No),
        WP_REACH("fp_flag", Objective, No),
        WP_REACH("skull", Objective, No),
        WP_REACH("assault_bomb", Objective, No),
    };
#undef WP_H3
#undef WP_ODST
#undef WP_REACH
#undef WP_H2
#undef WP_CE
#undef WP_H4

    // Row for a catalog model in `table`, or nullptr. Taking the table as input
    // lets tests exercise rows that set every field.
    template <size_t N>
    const Profile* FindProfile(const Profile (&table)[N], const weapon_model::Model* model) noexcept
    {
        if (!model || !model->name)
            return nullptr;
        for (const Profile& p : table)
            if (p.title == model->title && !std::strcmp(p.model, model->name))
                return &p;
        return nullptr;
    }

    // Always returns a profile: the row for this live identity, or kFallback.
    // Cost is a catalog scan plus a name scan. Fine for per-weapon-change callers;
    // a per-frame caller should cache the result per (title, identity).
    template <size_t N>
    const Profile& ProfileFor(const Profile (&table)[N], GameTitle title, uint64_t identity) noexcept
    {
        const Profile* p = FindProfile(table, weapon_model::Find(title, identity));
        return p ? *p : kFallback;
    }
    inline const Profile& ProfileFor(GameTitle title, uint64_t identity) noexcept
    {
        return ProfileFor(kProfiles, title, identity);
    }

    // The lens placement for a catalog weapon: an explicit captured row, else the
    // class starting placement for any Optic-zoom weapon. Returns false (and leaves
    // `out`) for weapons with no optic. `fromClass` says which one it was.
    template <size_t N>
    inline bool ScopeLensPlacement(const Profile (&table)[N], const weapon_model::Model* model, ScopeLensRow& out,
        bool& fromClass, const KitLensRow** kit = nullptr) noexcept
    {
        if (kit)
            *kit = nullptr;
        if (!model || !model->name)
            return false;
        if (const ScopeLensRow* row = ScopeLensRowFor(model->title, model->name))
        {
            out = *row;
            fromClass = false;
            return true;
        }
        if (const KitLensRow* k = KitLensRowFor(model->title, model->name))
        {
            out = k->row;
            fromClass = false;
            if (kit)
                *kit = k;
            return true;
        }
        const Profile* p = FindProfile(table, model);
        if (!p || p->zoomKind != ZoomKind::Optic)
            return false;
        out = ClassRow(model->title, model->name);
        fromClass = true;
        return true;
    }
    inline bool ScopeLensPlacement(
        const weapon_model::Model* model, ScopeLensRow& out, bool& fromClass, const KitLensRow** kit = nullptr) noexcept
    {
        return ScopeLensPlacement(kProfiles, model, out, fromClass, kit);
    }

    // ---- Weapon tuning (F1). A tuned zone is given as centre/half-length/radius
    // along the barrel; it applies only when all three are > 0.
    inline constexpr float kGrabCentreMaxM = 0.80f;
    inline constexpr float kGrabHalfLengthMaxM = 0.50f;
    inline constexpr float kGrabRadiusMaxM = 0.20f;
    constexpr bool GrabOverrideActive(float centreM, float halfLengthM, float radiusM) noexcept
    {
        return centreM > 0.0f && halfLengthM > 0.0f && radiusM > 0.0f;
    }
    inline GrabZone GrabZoneOverride(const GrabZone& profile, float centreM, float halfLengthM, float radiusM) noexcept
    {
        if (!GrabOverrideActive(centreM, halfLengthM, radiusM))
            return profile;
        const auto clamp = [](float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; };
        const float centre = clamp(centreM, 0.0f, kGrabCentreMaxM);
        const float half = clamp(halfLengthM, 0.0f, kGrabHalfLengthMaxM);
        GrabZone zone{};
        zone.alongMinM = centre - half > 0.0f ? centre - half : 0.0f;
        zone.alongMaxM = centre + half;
        zone.lateralMaxM = clamp(radiusM, 0.0f, kGrabRadiusMaxM);
        return zone;
    }

    // Field accessors on a resolved profile (fallback for unset fields).
    constexpr GrabZone GrabZoneOf(const Profile& p) noexcept
    {
        return p.hasGrabZone ? p.grabZone : GrabZone{};
    }
    // False: no authored offset; keep the config/per-weapon cfg path unchanged.
    constexpr bool GripOffsetOf(const Profile& p, Vec3& out) noexcept
    {
        if (!p.hasGripOffset)
            return false;
        out = p.gripOffsetM;
        return true;
    }
    // False: no lens known; keep the current scope presentation.
    constexpr bool ScopeLensOf(const Profile& p, Vec3& out) noexcept
    {
        if (!p.hasScopeLens)
            return false;
        out = p.scopeLensM;
        return true;
    }

    // Query API for features: live title + weapon_model identity.
    inline SizeClass WeaponClassFor(GameTitle title, uint64_t identity) noexcept
    {
        return ProfileFor(title, identity).size;
    }
    inline bool IsScoped(GameTitle title, uint64_t identity) noexcept
    {
        return ProfileFor(title, identity).scoped;
    }
    // None for unknown weapons (fallback), as for unscoped ones.
    inline ZoomKind ZoomKindFor(GameTitle title, uint64_t identity) noexcept
    {
        return ProfileFor(title, identity).zoomKind;
    }
    inline bool ZoomKindVerified(GameTitle title, uint64_t identity) noexcept
    {
        return ProfileFor(title, identity).zoomVerified;
    }
    // A catalog weapon whose row says, verified, that it has no zoom at all (an
    // AR, a plasma pistol): look-to-scope never arms for it. Unknown weapons and
    // unverified rows keep the old behaviour.
    inline bool KnownNoZoom(GameTitle title, uint64_t identity) noexcept
    {
        const Profile* p = FindProfile(kProfiles, weapon_model::Find(title, identity));
        return p && p->zoomKind == ZoomKind::None && p->zoomVerified;
    }
    // A gun whose glass stays live while held. It must be an
    // Optic row AND have a kit lens node (the glass anchored to the rendered gun);
    // optic guns without measured glass (rocket launcher, spartan laser, carbine
    // in H3/ODST) keep the look/button engage. CE Classic uses its own pass.
    inline bool LiveGlassGun(GameTitle title, uint64_t identity) noexcept
    {
        if (ZoomKindFor(title, identity) != ZoomKind::Optic)
            return false;
        const weapon_model::Model* model = weapon_model::Find(title, identity);
        return model && KitLensNodeFor(title, model->name) != nullptr;
    }
    inline const char* ZoomKindName(ZoomKind kind) noexcept
    {
        return kind == ZoomKind::Optic ? "optic" : kind == ZoomKind::Visor ? "visor" : "none";
    }
    inline bool CanDualWield(GameTitle title, uint64_t identity) noexcept
    {
        return ProfileFor(title, identity).dualWield;
    }
    inline GrabZone GrabZoneFor(GameTitle title, uint64_t identity) noexcept
    {
        return GrabZoneOf(ProfileFor(title, identity));
    }
    inline bool GripOffsetFor(GameTitle title, uint64_t identity, Vec3& out) noexcept
    {
        return GripOffsetOf(ProfileFor(title, identity), out);
    }
    inline bool ScopeLensFor(GameTitle title, uint64_t identity, Vec3& out) noexcept
    {
        return ScopeLensOf(ProfileFor(title, identity), out);
    }

    // The held gun's identity for the per-gun overlay (lens row, alignment). A live
    // read can be 0 for single updates (CE's gun graph needs a palette apply within
    // 150 ms); restoring the global fallback for those flapped the CE sniper glass
    // between its 0.040 m row and the 0.159 m default. Like the zoom
    // gate, keep the last gun through up to gapMs of zeros; hold (pause/F1) keeps
    // it indefinitely. A different live gun replaces it at once.
    struct IdentityBridge
    {
        uint64_t identity = 0, liveMs = 0;
        uint64_t Update(uint64_t live, uint64_t nowMs, bool hold, uint64_t gapMs = 1000) noexcept
        {
            if (live || hold)
            {
                if (live)
                    identity = live;
                liveMs = nowMs;
            }
            else if (identity && nowMs - liveMs > gapMs)
                identity = 0;
            return identity;
        }
        void Reset() noexcept
        {
            identity = 0;
            liveMs = 0;
        }
    };
} // namespace weapon_profile
