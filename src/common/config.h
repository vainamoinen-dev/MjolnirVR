// Every MjolnirVR setting: the Config struct, its defaults, the per-game and per-seat tables, and the global g_config.
// config.cpp loads it from and saves it to the settings file. The DLL core, the title adapters, the launcher and the
// tests include it. Header only: the declared functions live in config.cpp.
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include "vr_action_mapping.h"
#include "virtual_stock_settings.h"
#include "hud_element_logic.h"

struct ConfigVehicleModelTrim
{
    GameTitle title = GameTitle::None;
    uint64_t identity{};
    int16_t seat = -1;
    float value[3]{};
    bool set[3]{};
};
inline constexpr unsigned kVehicleModelTrimCapacity = 128;

// Every supported MCC title shares one halomccvr.cfg next to the DLL, as plain
// "key = value" text. These are portable user preferences; title-specific
// camera, weapon, skeleton, and HUD calibration belongs in the title adapter.
// The in-headset menu edits the same values live and saves them back. Saving
// rewrites the whole file, so hand-written comments do not survive (values do).

// MCC's VR raster size at resolution_scale 1.00, and the range a hand-edited
// scale may take. The launcher turns these into -ResX/-ResY; the DLL upscales
// the finished eye into the unchanged full-size OpenXR projection. Both the
// launcher and the config clamp read these so the two can never disagree.
inline constexpr int kNativeRenderWidth = 2912;
inline constexpr int kNativeRenderHeight = 2100;
inline constexpr float kResolutionScaleMin = 0.35f;
inline constexpr float kPhysicalMeleeSpeedMin = 0.3f;
inline constexpr float kPhysicalMeleeSpeedMax = 10.0f;
// 2026-10-09 prior art (full 3-D hand speed 2.0-2.5 m/s, 0.2-0.5 s cooldown):
// v7 defaults; older configs keep 5.0 m/s and the 60 ms contact rearm.
inline constexpr float kPhysicalMeleeSpeedDefault = 2.2f;
inline constexpr float kPhysicalMeleeSpeedLegacyDefault = 5.0f;
inline constexpr float kPhysicalMeleeCooldownDefaultS = 0.30f;
inline constexpr float kPhysicalMeleeCooldownLegacyS = 0.06f;
inline constexpr float kPhysicalMeleeCooldownMinS = 0.06f;
inline constexpr float kPhysicalMeleeCooldownMaxS = 1.00f;
inline float ClampPhysicalMeleeCooldown(float seconds) noexcept
{
    return std::isfinite(seconds) ? std::clamp(seconds, kPhysicalMeleeCooldownMinS, kPhysicalMeleeCooldownMaxS)
                                  : kPhysicalMeleeCooldownDefaultS;
}
// The swing speed every contact-melee title uses (the load clamp uses it too).
inline float ClampPhysicalMeleeSwingSpeed(float value) noexcept
{
    return std::clamp(value, kPhysicalMeleeSpeedMin, kPhysicalMeleeSpeedMax);
}
// 2.75 renders ~8008x5775 (8K-class width). The "Keith David" F1 tier lands on
// true 8K width (7680, scale ~2.64); this ceiling leaves a little headroom above
// it. Everything is a uniform multiplier, so 2912:2100 (Halo's VR aspect) is
// preserved at every value. Above ~1.76 (5k-class) is extremely heavy; the F1
// menu warns there (see menu.cpp) but nothing is blocked.
inline constexpr float kResolutionScaleMax = 2.75f;
// F1/help "past this is very heavy" threshold: ~5k-class width (5120/2912).
inline constexpr float kResolutionScaleHeavy = 1.76f;
// Draw-distance trim: fraction of the engine's stock render far-clip distance
// (stock 10240 world units). 1.00 = full stock draw distance. The floor reaches
// deep enough to pull the far plane INTO the playable scene (~512 units), so low
// settings cull real terrain/objects, not just the distant skybox. Most levels
// only start visibly culling below ~0.25 because nearby geometry is closer than
// the far plane; the lowest settings clip near geometry (hard pop-in) for frames.
inline constexpr float kDrawDistanceMin = 0.05f;
inline constexpr float kDrawDistanceMax = 1.00f;
inline constexpr float kHudCurvatureMin = 0.00f;
inline constexpr float kHudCurvatureMax = 1.00f;
inline constexpr float kHudAspectMin = 0.50f;
inline constexpr float kHudAspectMax = 2.00f;
inline constexpr float kHudHeightMin = -300.0f;
inline constexpr float kHudHeightMax = 300.0f;

// Where the F1 menu panel sits in the headset, in meters. These four values are
// the ONLY description of the panel's placement: both the composition quad and
// the laser-pointer raycast read them, so the two can never disagree. Until
// 2026-07-29 each carried its own copy of the same literals (1.2 / 1.1 / -0.08),
// which meant moving the panel would have left the pointer behind.
inline constexpr float kMenuDistanceMin = 0.40f;
inline constexpr float kMenuDistanceMax = 6.00f;
inline constexpr float kMenuWidthMin = 0.40f;
inline constexpr float kMenuWidthMax = 4.00f;
// Lateral/vertical travel of the grab handle, measured from straight ahead.
inline constexpr float kMenuOffsetLimit = 3.00f;

// Per-SEAT seat-trim overrides (an earlier version keyed these per vehicle;
// each seat needs its own). The vehicle axis is Halo3VehicleId - 1
// and the seat axis is the seat slot below. Config stays decoupled from the
// enum header, so the order here mirrors it by contract and game.cpp
// static_asserts the two never drift. "prowler" is the Mauler enum entry (its
// user-facing name); "turret" is every walk-up turret.
inline constexpr int kVehicleTrimCount = 10;
inline constexpr const char* kVehicleTrimNames[kVehicleTrimCount] = {
    "scorpion", "warthog", "mongoose", "ghost", "wraith", "prowler", "banshee", "hornet", "chopper", "turret"};

// Seat slots within one vehicle: the three authored passenger indices plus
// the mounted-turret gunner, which shares seat index 0 with the driver and so
// needs a slot of its own. Halo 3 authors no vehicle with more than three
// non-turret seats (kHalo3SeatPoints).
inline constexpr int kVehicleSeatSlots = 4;
inline constexpr int kVehicleGunnerSlot = 3;
inline constexpr const char* kVehicleSeatNames[kVehicleSeatSlots] = {"driver", "passenger", "passenger2", "gunner"};
inline constexpr int kVehicleTrimSlots = kVehicleTrimCount * kVehicleSeatSlots;

// ODST keeps its own bank of per-seat trims (user directive, 2026-08-03) under
// `_odst_`-prefixed keys, so tuning a warthog in ODST never moves the Halo 3
// warthog and vice versa. It needs a wider table than Halo 3's for two
// evidence-backed reasons: ODST clears the
// scorpion riders' "invalid for player" bit, so seat indices 1-4 are all
// enterable, and ODST adds the shade as a drivable vehicle Halo 3 never
// exposed. The Halo 3 names, slots and key spellings above are untouched, so no
// existing config line changes meaning.
inline constexpr int kOdstVehicleTrimCount = 11;
inline constexpr const char* kOdstVehicleTrimNames[kOdstVehicleTrimCount] = {
    "scorpion", "warthog", "mongoose", "ghost", "wraith", "prowler", "banshee", "hornet", "chopper", "turret", "shade"};
inline constexpr int kOdstVehicleSeatSlots = 6;
inline constexpr int kOdstVehicleGunnerSlot = 5;
inline constexpr const char* kOdstVehicleSeatNames[kOdstVehicleSeatSlots] = {
    "driver", "passenger", "passenger2", "passenger3", "passenger4", "gunner"};
inline constexpr int kOdstVehicleTrimSlots = kOdstVehicleTrimCount * kOdstVehicleSeatSlots;

// Reach keeps a third, independent trim bank under `_reach_`-prefixed keys.
// The vehicle order is the ReachVehicleId runtime contract. Every name below
// is a stable top-level vehicle identity present in the pinned official HREK
// tag tree. Attached child tags stay distinct when HREK gives them their own
// model/seat parent (Wraith gunner, Warthog weapons and Shade weapons); these
// are not guessed Halo 3 mounted-gunner variants. Falcon authors eleven seats,
// so the bank reserves sixteen raw indices without assigning roles to unused
// slots. Keep the keys role-neutral: seat 0 is not always a
// driver and later assigning a role must not change an existing config key.
// Rows 21-34 are the 2026-08-04 full official census
// of HREK vehicle tags: every remaining player-usable
// HREK vehicle tag. Their slots ship unset, so they follow the universal trim
// until tuned live in F1. Rows 1-20 carry the completed 25-camera
// Blender lineup through kConfigReachShippedSeatTrims below. Existing keys keep
// their exact spelling.
// Row 35 is not an identity. Before it existed, sitting in a vehicle
// whose tuple did not resolve left the F1 sliders bound to the UNIVERSAL trim,
// so a user adjusting that seat silently rewrote the base every unadjusted
// Halo 3, ODST and Reach seat follows. A 2026-08-06 play session did exactly that
// (universal 0.10/0.05 -> -0.765/0.894 while seated in an unmatched turret).
// Halo 3's contract is "sitting in a seat always adjusts that seat alone", so
// every unmatched Reach seat now shares this one row instead.
inline constexpr int kReachVehicleIdentityTrimCount = 34;
inline constexpr int kReachVehicleTrimCount = kReachVehicleIdentityTrimCount + 1;
inline constexpr int kReachUnmatchedVehicleTrimId = kReachVehicleTrimCount;
inline constexpr const char* kReachVehicleTrimNames[kReachVehicleTrimCount] = {"banshee", "space_banshee", "ghost",
    "revenant", "wraith", "wraith_gunner", "mongoose", "warthog", "warthog_chaingun", "warthog_gauss", "warthog_rocket",
    "falcon", "sabre", "scorpion", "forklift", "cart", "shade_plasma", "shade_flak", "plasma_turret", "machinegun",
    "mac_cannon", "scorpion_anti_infantry", "seraph", "pelican", "pelican_chin_gun", "corvette_cannon",
    "space_phantom_chin_gun", "space_phantom_beam_turret", "cargo_truck", "military_truck", "oni_van", "pickup",
    "truck_cab_large", "squad_drop_pod", "unmatched"};
inline constexpr int kReachVehicleSeatSlots = 16;
inline constexpr const char* kReachVehicleSeatNames[kReachVehicleSeatSlots] = {"seat0", "seat1", "seat2", "seat3",
    "seat4", "seat5", "seat6", "seat7", "seat8", "seat9", "seat10", "seat11", "seat12", "seat13", "seat14", "seat15"};
inline constexpr int kReachVehicleTrimSlots = kReachVehicleTrimCount * kReachVehicleSeatSlots;

// Vehicle placement controls are authored in metres, then converted to Halo
// world units at the same boundary as the Blender seat point. Forward/height
// have a little more travel than the original -0.5..1.0 sliders; lateral trim
// is symmetric because it moves across the width of the seat.
inline constexpr float kVehicleCamForwardMin = -1.00f;
inline constexpr float kVehicleCamForwardMax = 1.50f;
inline constexpr float kVehicleCamUpMin = -1.00f;
inline constexpr float kVehicleCamUpMax = 1.50f;
inline constexpr float kVehicleCamRightMin = -1.00f;
inline constexpr float kVehicleCamRightMax = 1.00f;
// The shipped universal trim. Named so the F1 panel can offer a one-click
// reset: this one triplet is shared by Halo 3, ODST and every Reach seat with
// no line of its own, so a wrong value here moves seats in all three titles.
inline constexpr float kVehicleCamForwardDefault = 0.10f;
inline constexpr float kVehicleCamUpDefault = 0.05f;
inline constexpr float kVehicleCamRightDefault = 0.0f;

// Reach's HREK marker seeds are not consistently authored at the player's
// eye. The validated 25-seat Blender lineup therefore contains legitimate
// marker-to-eye deltas much larger than the close-range trims used by Halo 3
// and ODST (the Sabre is the largest). Keep this title-specific so the
// established sliders and clamps for the other engines do not change.
inline constexpr float kReachVehicleCamForwardMin = -64.0f;
inline constexpr float kReachVehicleCamForwardMax = 64.0f;
inline constexpr float kReachVehicleCamUpMin = -16.0f;
inline constexpr float kReachVehicleCamUpMax = 16.0f;
inline constexpr float kReachVehicleCamRightMin = -16.0f;
inline constexpr float kReachVehicleCamRightMax = 16.0f;

// The storage slot for one seat, or -1 when the seat cannot be keyed (on
// foot, unidentified vehicle, or a seat index Halo 3 never authors).
inline constexpr int ConfigSeatTrimSlot(int vehicleId, int seatIndex, bool mountedTurret)
{
    const int v = vehicleId - 1;
    if (v < 0 || v >= kVehicleTrimCount)
        return -1;
    if (mountedTurret)
        return v * kVehicleSeatSlots + kVehicleGunnerSlot;
    // A rider seat may only take a rider slot: the gunner slot is reserved,
    // so a fourth authored seat keys nothing rather than silently sharing the
    // mounted gunner's trim.
    if (seatIndex < 0 || seatIndex >= kVehicleGunnerSlot)
        return -1;
    return v * kVehicleSeatSlots + seatIndex;
}

// The same mapping over ODST's wider table. `vehicleId` is the raw
// OdstVehicleId value (1 = scorpion .. 11 = shade), mirroring
// kOdstVehicleTrimNames by contract; game.cpp static_asserts the two.
inline constexpr int ConfigOdstSeatTrimSlot(int vehicleId, int seatIndex, bool mountedTurret)
{
    const int v = vehicleId - 1;
    if (v < 0 || v >= kOdstVehicleTrimCount)
        return -1;
    if (mountedTurret)
        return v * kOdstVehicleSeatSlots + kOdstVehicleGunnerSlot;
    if (seatIndex < 0 || seatIndex >= kOdstVehicleGunnerSlot)
        return -1;
    return v * kOdstVehicleSeatSlots + seatIndex;
}

// Reach indexes the adapter's stable HREK parent identity and seat index
// directly. Distinct child seat parents already have distinct identities, so
// no Halo 3-style synthetic mounted flag is needed here.
inline constexpr int ConfigReachSeatTrimSlot(int vehicleId, int seatIndex)
{
    const int v = vehicleId - 1;
    if (v < 0 || v >= kReachVehicleTrimCount || seatIndex < 0 || seatIndex >= kReachVehicleSeatSlots)
        return -1;
    return v * kReachVehicleSeatSlots + seatIndex;
}

// Seat placements the maintainer tuned in the headset and which therefore ship
// as the product's own defaults, exactly as the released ZIP ships their live
// config. Written 2026-08-01 from the Game Pass session that accepted the
// first-person vehicle work. Config parsing performs no migration, so a user
// keeping an older file silently falls back to these built-ins — baking them in
// is what stops that fallback from being untuned.
//
// Each entry names its vehicle by Halo3VehicleId value (1 = scorpion .. 10 =
// turret, mirroring kVehicleTrimNames) and its seat by index, or by the mounted
// flag for a turret gunner. Only the axes the maintainer actually moved are
// marked set, so every other axis still follows the universal trim.
struct ConfigShippedSeatTrim
{
    int vehicleId;
    int seatIndex;
    bool mountedTurret;
    float forward;
    float up;
    float right;
    bool hasForward;
    bool hasUp;
    bool hasRight;
};

// clang-format off
inline constexpr ConfigShippedSeatTrim kConfigShippedSeatTrims[] = {
    // vehicle, seat, mounted,  forward,  up,     right,  F,     U,     R
    {1, 0, false,  0.29f,  1.50f, -0.03f, true,  true,  true},  // scorpion driver
    {2, 0, false, -0.04f,  0.00f,  0.00f, true,  false, false}, // warthog driver
    {2, 1, false, -0.28f,  0.55f, -0.10f, true,  true,  true},  // warthog passenger
    {6, 0, true,   0.00f,  0.30f,  0.00f, false, true,  false}, // prowler gunner
    {8, 0, false, -0.23f, -0.09f, -0.03f, true,  true,  true},  // hornet driver
    {9, 0, false,  0.12f, -0.06f,  0.00f, true,  true,  false}, // chopper driver
    // clang-format on
};

// ODST's shipped bank. Seeded from the Halo 3 values above for every seat the
// tag diff proved geometrically identical (O-E1: same nodes, same markers
// within 1e-4 wu, same seat flags), so ODST feels right the first time it is
// driven instead of starting from an untuned universal trim. Tuning a seat in
// ODST afterwards writes only the ODST bank. Vehicle ids follow
// kOdstVehicleTrimNames (1 = scorpion .. 11 = shade); the ODST-only seats
// (scorpion riders, shade) ship unset and follow the universal trim until the
// headset says otherwise.
// clang-format off
inline constexpr ConfigShippedSeatTrim kConfigOdstShippedSeatTrims[] = {
    // vehicle, seat, mounted,  forward,  up,     right,  F,     U,     R
    {1, 0, false,  0.29f,  1.50f, -0.03f, true,  true,  true},  // scorpion driver
    {2, 0, false, -0.04f,  0.00f,  0.00f, true,  false, false}, // warthog driver
    {2, 1, false, -0.28f,  0.55f, -0.10f, true,  true,  true},  // warthog passenger
    {6, 0, true,   0.00f,  0.30f,  0.00f, false, true,  false}, // prowler gunner
    {8, 0, false, -0.23f, -0.09f, -0.03f, true,  true,  true},  // hornet driver
    {9, 0, false,  0.12f, -0.06f,  0.00f, true,  true,  false}, // chopper driver
    // clang-format on
};

// The user's completed Reach Blender placement export, converted by the kit's
// documented authored = seed + {forward, -right, up} equation and persisted at
// halomccvr.cfg's two-decimal precision. These are user-authored,
// headset-unverified camera placements, not guessed HREK eye points.
// all_cameras_marked_placed is true
// for all 25 rows in reach_vehicle_camera_points.json, so all three axes are
// intentional (including zero) and ship set. Vehicle ids mirror
// kReachVehicleTrimNames (1 = banshee .. 20 = machinegun).
struct ConfigReachShippedSeatTrim
{
    int vehicleId;
    int seatIndex;
    float forward;
    float up;
    float right;
};

// clang-format off
inline constexpr ConfigReachShippedSeatTrim kConfigReachShippedSeatTrims[] = {
    // vehicle, seat, forward, up, right
    { 1, 0,   1.94f, -1.17f,  0.00f}, // banshee
    { 2, 0,  11.65f, -3.07f,  0.00f}, // space_banshee
    { 3, 0,   0.00f,  1.13f,  0.00f}, // ghost
    { 4, 0,   0.48f,  1.12f,  0.00f}, // revenant
    { 4, 1,   0.00f,  0.35f, -0.73f}, // revenant
    { 5, 0,  -3.70f,  3.52f,  0.00f}, // wraith
    { 6, 0,   0.00f,  0.00f,  0.00f}, // wraith_gunner
    { 7, 0,   0.45f,  0.86f,  0.00f}, // mongoose
    { 7, 1,  -0.12f, -0.21f,  0.00f}, // mongoose
    { 8, 0,   0.13f,  0.79f,  0.00f}, // warthog
    { 8, 1,  -0.21f, -0.22f, -0.23f}, // warthog
    { 9, 0,  -1.29f,  0.72f,  0.00f}, // warthog_chaingun
    {10, 0,  -1.21f,  1.10f,  0.00f}, // warthog_gauss
    {11, 0,  -1.37f,  0.69f,  0.00f}, // warthog_rocket
    {12, 0,  -0.09f,  0.87f,  0.00f}, // falcon
    {12, 3,   0.00f,  0.24f,  0.21f}, // falcon
    {12, 4,  -0.06f,  0.33f,  0.00f}, // falcon
    {13, 0,  42.43f, -8.18f,  0.00f}, // sabre
    {14, 0,   0.00f,  2.69f,  0.00f}, // scorpion
    {15, 0,   0.33f,  0.49f,  0.00f}, // forklift
    {16, 0,   0.00f,  0.00f,  0.00f}, // cart
    {17, 0,   0.00f,  2.41f,  0.00f}, // shade_plasma
    {18, 0,   0.11f,  2.27f,  0.00f}, // shade_flak
    {19, 0,   0.00f,  0.24f,  0.00f}, // plasma_turret
    {20, 0,   0.00f,  0.26f,  0.00f}, // machinegun
    // clang-format on
};
inline constexpr int kConfigReachShippedSeatTrimCount =
    sizeof(kConfigReachShippedSeatTrims) / sizeof(kConfigReachShippedSeatTrims[0]);

// Per-title profiles: the thirteen weapon/hand/HUD tunables every title keeps its
// own copy of - one struct so a profile is copied whole. Defaults mirror
// the shared fields' defaults so an untouched profile behaves identically.
inline constexpr int kTitleProfileCount = 7;

// The default helmet-zoom window as edges (today's 0.133 x 1.5
// window at (0.032, -0.020), 4:3), shared by TitleTunables and Config.
inline constexpr float kVisorDefaultLeftM = -0.06775f;
inline constexpr float kVisorDefaultRightM = 0.13175f;
inline constexpr float kVisorDefaultTopM = 0.0548125f;
inline constexpr float kVisorDefaultBottomM = -0.0948125f;
struct TitleTunables
{
    float gun_scale = 0.96f;
    float left_hand_scale = 0.96f;
    // Visible hand meshes only, in each hand's local frame (metres).
    float left_hand_mesh_x_m = 0.0f;
    float left_hand_mesh_y_m = 0.0f;
    float left_hand_mesh_z_m = 0.0f;
    float right_hand_mesh_x_m = 0.0f;
    float right_hand_mesh_y_m = 0.0f;
    float right_hand_mesh_z_m = 0.0f;

    float gun_pitch_deg = -3.0f;
    float gun_yaw_deg = 0.0f;
    float gun_roll_deg = 0.0f;
    float gun_forward_m = -0.14f;
    float gun_right_m = 0.0f;
    float gun_up_m = 0.0f;
    float left_hand_forward_m = -0.063f;
    float hud_size = 0.43f;
    float hud_aspect = 1.22f;
    float hud_curvature = 0.48f;
    float hud_vertical_offset = 16.0f;
    // Mesh-only barrel trim: rotates the visible gun (and the hands on it)
    // about the controller WITHOUT moving the crosshair or the shot ray.
    // Per game, like everything else here. 0 = untouched.
    float barrel_pitch_deg = 0.0f;
    float barrel_yaw_deg = 0.0f;
    float barrel_roll_deg = 0.0f;
    // The picture is per game too. Reach's own values come from
    // v24 (FXAA at 1.30); every other game starts from the shared ones.
    float resolution_scale = 1.0f;
    int aa_mode = 0;
    // The helmet-zoom window, per game (see Config's visor_zoom_*).
    float visor_zoom_left_m = kVisorDefaultLeftM;
    float visor_zoom_right_m = kVisorDefaultRightM;
    float visor_zoom_top_m = kVisorDefaultTopM;
    float visor_zoom_bottom_m = kVisorDefaultBottomM;
    float visor_zoom_scale = 1.0f;
    float visor_zoom_distance_m = 0.40f;
};

// The one settings-schema version (struct default, Clamp, loader messages).
// v23: a saved file ends with "config_complete = 1"; a v23+ file without it was cut short.
// v24: Reach's own anti-aliasing and resolution scale (FXAA at 1.30).
// v25: the helmet-zoom window as four edges, a size and a distance.
inline constexpr int kConfigVersion = 25;
// v24's Reach values (headset feedback 2026-10-14: "make sure Reach's graphic settings are top notch").
inline constexpr int kReachAaModeV24 = 1;
// Reach's slot in Config::title_profiles (prefix "reach_"; core_tests pins it).
inline constexpr int kReachTitleProfile = 2;
inline constexpr float kReachResolutionScaleV24 = 1.30f;
// Files older than this load their current keys without the old
// migrations (v2-v11 retired); the oldest config backups still in use are v12.
inline constexpr int kOldestMigratedConfigVersion = 12;

struct Config
{
    // Stamps the Halo 3, ODST and Reach shipped seat tables into their
    // independent per-seat arrays. Every other member keeps its default member
    // initializer, so Config{} remains the one description of a fresh
    // configuration.
    Config();

    int config_version = kConfigVersion;

    // Portable OpenXR feedback and pose stabilization. Headset smoothing is a
    // deliberately tiny previous-frame blend (0.03 shipped default, 10% hard maximum)
    // so users may remove micro-jitter without turning head motion into a laggy camera.
    // Aim stabilization affects only the floating VR crosshair; weapon aim and
    // bullets continue to use the current raw controller pose.
    float haptic_intensity = 0.86f;
    float headset_smoothing = 0.03f;
    float aim_stabilization = 0.48f;

    float screen_width_m = 4.0f;    // width of the virtual screen, in meters
    float screen_distance_m = 2.4f; // how far away the screen floats, in meters

    // Universal cutscene-only stereo theatre. A title enters it only after its
    // adapter proves the engine owns an authored camera and player camera
    // control is unavailable. Future title adapters inherit the same settings.
    bool cutscene_theater_enabled = true;
    // Fade to black on both ends of every 3D <-> theatre/flat switch (ms).
    // v7 150; older configs 100 (the previous fixed theatre fade).
    float cutscene_fade_ms = 150.0f;
    // Authored sections that stay in 3D with free head look instead of the
    // theatre, e.g. "odst:3, reach:*" (scene indices are logged on entry).
    char cutscene_3d_sections[160] = {};
    float cutscene_theater_depth = 1.0f; // 0 flat, 1 runtime IPD, 2 double
    bool cutscene_theater_flip_depth = false;
    float cutscene_theater_width_m = 6.0f;
    float cutscene_theater_distance_m = 4.0f;
    // Black cine bars over the parts of the authored frame a monitor never
    // shows. The cutscene is rasterized into the headset's render shape, which
    // is taller than 16:9, so without them the theatre exposes scene above and
    // below the intended shot. 0 disables the bars.
    float cutscene_theater_matte_aspect = 16.0f / 9.0f;
    // Slides the retained window up (positive) or down inside that frame, as a
    // fraction of the frame height. The window never changes size.
    float cutscene_theater_matte_offset = 0.0f;
    // Put the title's own subtitle text back on the theatre screen. Subtitles
    // are interface text drawn after both eye captures, so they reach the
    // monitor and never the headset without this.
    bool cutscene_theater_subtitles = true;
    // How much of the bottom of the title's finished frame is searched for that
    // text, as a fraction of its height. Text drawn above this is not found.
    float cutscene_theater_subtitle_band = 0.30f;
    // Diagnostic: paint that strip onto the theatre screen exactly as captured,
    // with no glyph selection at all, to show what the mod is actually reading.
    bool cutscene_theater_subtitle_debug = false;

    // F1 menu panel placement. The panel is its own composition quad, separate
    // from the virtual screen above. The defaults are the fixed constants the
    // panel used before it became movable. The grab handle at the top of the
    // panel writes these live and saves on release; "Reset panel position" in
    // the Advanced category puts them back.
    float menu_distance_m = 1.2f; // how far in front of you the panel floats
    float menu_width_m = 1.1f;    // panel width; height follows the 4:3 texture
    float menu_height_m = -0.08f; // vertical offset, negative = below eye line
    float menu_side_m = 0.0f;     // sideways offset, positive = to your right
    // Menu laser delivery: 0 = real absolute mouse moves (SendInput; MCC's
    // menus follow the laser), 1 = legacy SetCursorPos.
    bool menu_pointer_setcursorpos = false;
    bool pause_menu_world_lock = true; // the paused game screen stays where you were looking when you paused
    bool game_menu_pointer = true;     // controller mouse in native menus (default on since v7)
    // Save & Quit dialog (every title): when the game ends a pause without a
    // resume press, A = Enter and stick left/right = arrows to MCC until the
    // first gameplay input (trigger, walking) or 20 s.
    bool menu_dialog_keys = true;

    // Show the welcome page automatically, once, at the start of each launch.
    // The mod has no other way to tell a player anything before they play: the
    // README is outside the game and this file's comments are only read by
    // people who open it. Ticking "Don't show this again" on that page clears
    // this. It suppresses only the AUTOMATIC appearance -- the Welcome page
    // stays in the F1 menu so the message can be read again at any time.
    bool show_welcome = true;

    // VR controller turning (right Sense stick).
    bool roomscale_movement = false; // physical horizontal steps drive native walking
    bool physical_running = false;
    float physical_running_speed = 1.0f; // fraction of native maximum locomotion
    float physical_running_sensitivity = 1.0f;
    // Immersive (v7): crouch by physically crouching (no crouch button).
    bool physical_crouch = true;
    float physical_crouch_depth_m = 0.22f;
    bool turn_smooth = true;          // false = snap turn, true = smooth turn
    bool vehicle_smooth_turn = false; // temporary smooth VR turning while seated
    float turn_snap_deg = 30.0f;      // degrees per snap
    float turn_smooth_deg_s = 120.0f; // smooth turn speed, degrees/second

    // Send Start when Y and B are pressed together. This is the pause fallback
    // for titles whose controller Menu button is reserved by the OpenXR runtime.
    // Off for new (v7) configs: B sprint + Y switch within the chord window would
    // pause mid-fight; Touch's left menu button is Start. v6-and-older keep on.
    bool y_b_start_chord = false;

    // Halo 2 only: the gamepad's Back/View button is MCC's instant
    // Classic <-> Anniversary graphics switch. 0 = the mod swallows that
    // button in Halo 2 so a physical pad (Steam Controller etc.) cannot flip
    // the renderer mid-game; 1 = pass it through as the game expects.
    bool halo2_gamepad_graphics_switch = false;

    // Which controller, held next to the head, turns the left stick into the
    // D-pad (head gesture): 0 = left controller, 1 = right controller.
    int dpad_hand = 0;
    // Head-gesture activation radius in metres; the existing reach is 30 cm.
    float dpad_head_radius = 0.30f;
    // Quest 3 alternate D-pad: touch the physical left thumb rest, then use
    // the physical right stick. Handedness never swaps these controls.
    bool quest_thumbrest_dpad = false;
    bool disable_flashlight_input = false;
    // Match each campaign's MCC layout; same title order as gesture buttons.
    int flashlight_button[6]{15, 15, 15, 15, 15, 15}; // legacy migration / unverified-title fallback
    // Index = weapon_interaction::TitleIndex: H3, ODST, Reach, H4, CE, H2.
    vr_mapping::Overrides vr_bindings[6]{vr_mapping::ImmersiveBindings(GameTitle::Halo3),
        vr_mapping::ImmersiveBindings(GameTitle::Halo3ODST), vr_mapping::ImmersiveBindings(GameTitle::HaloReach),
        vr_mapping::ImmersiveBindings(GameTitle::Halo4), vr_mapping::ImmersiveBindings(GameTitle::HaloCE),
        vr_mapping::ImmersiveBindings(GameTitle::Halo2)};
    bool vr_action_mapping = true;
    bool flashlight_suppress_on_two_hand = true;
    // Optional temporal upscaler; ordinary eye resolve remains the default.
    int upscaler = 0;
    int dlss_mode = 1;
    bool dlss_jitter = true;
    int dlss_preset = 0;
    bool dlss_debug_view = false;
    bool vr_gameplay_subtitles = true;
    float vr_gameplay_subtitle_scale = 1.f;
    float vr_gameplay_subtitle_x = 0.f, vr_gameplay_subtitle_y = 0.f;
    int vr_gameplay_subtitle_anchor = 0;
    float vr_theatre_subtitle_scale = 1.f;
    float vr_theatre_subtitle_x = 0.f, vr_theatre_subtitle_y = 0.f;
    int vr_theatre_subtitle_anchor = 0;

    // Aim crosshair (stereo only): a small reticle floating where the weapon
    // actually shoots. Drawn as a compositor quad layer, so it costs no game
    // First-person vehicles (currently Halo 3; other titles ignore these
    // until they publish a vehicle state). The camera sits at the engine's
    // Blender-authored point instead of the third-person chase position; head
    // look and leaning stay fully live. 0 restores the stock chase view.
    // v17 (headset feedback 2026-10-11): vehicles start in the game's third-person
    // camera while zoom is the priority; the seat toggle and F1 still go first person.
    bool vehicle_first_person = false; // LIVE seat-view flag (the seat toggle changes it per seated vehicle)
    bool vehicle_first_person_cfg =
        false; // what the config file says; the live flag returns to it when you leave a vehicle
    // Halo CE vehicles (found in headset testing). Seat eye: apply the kit seat eye
    // (seat + 0.72 m) on the checked hull frame without waiting for the motion
    // proof (which needs real driving); every frame: place the eye on every
    // camera frame while seated, not only on a fresh selector verdict; turret
    // aim: aim from a reference frozen in the hull at seat entry, not the
    // turret-mounted camera (which turned with the aim: endless spin).
    bool ce_vehicle_eye_provisional = true;
    bool ce_vehicle_eye_every_frame = true;
    bool ce_turret_aim_frozen = true;
    // CE Anniversary graphics has no extra render, so: steal one frame in
    // ce_anniv_monitor_every, only while the head turned at most
    // ce_anniv_monitor_still_deg since the previous frame, to render the
    // visor zoom or a hull camera instead of the eyes (that frame repeats
    // the previous eye pair).
    bool ce_anniv_monitor_frames = true;
    int ce_anniv_monitor_every = 3;
    // The gun camera on the lens glass, raised to the eye, borrows one frame in
    // this many (each borrowed frame repeats the eye pair, so fewer = a steadier world; the
    // glass is reprojected by the gun's rotation in between). At the hip: 1 in 12.
    int ce_anniv_glass_raised_every = 6;
    // Deliberate deviation from the one-in-N rule: right after zoom-on, steal one frame in
    // ce_anniv_zoom_boost_every for ce_anniv_zoom_boost_frames samples so the
    // zoom fills in fast, then ce_anniv_monitor_every. 0 frames = off.
    // Visor zoom only (the raised glass never boosts).
    int ce_anniv_zoom_boost_frames = 90;
    int ce_anniv_zoom_boost_every = 2;
    float ce_anniv_monitor_still_deg = 0.5f;
    // Hull (vehicle screen) frames while driving: one in N (0 = only when
    // stopped). Lets the driver see out while driving; N is tuned in the headset.
    int ce_anniv_hull_frames_moving_every = 9;
    // + = toward the windshield / raise out of the seat / vehicle's right.
    float vehicle_cam_forward_m = kVehicleCamForwardDefault;
    float vehicle_cam_up_m = kVehicleCamUpDefault;
    float vehicle_cam_right_m = kVehicleCamRightDefault;
    // Per-game fallback values. Unset axes inherit the legacy global trim so
    // existing cfg files retain their exact camera position until edited.
    float vehicle_cam_game[6][3]{}; // title order; forward/up/right
    bool vehicle_cam_game_set[6][3]{};
    ConfigVehicleModelTrim vehicle_model_trims[kVehicleModelTrimCapacity]{};
    // Per-SEAT overrides of the three trims above. An entry only exists once the
    // user adjusts the F1 sliders while SITTING IN that seat (or writes the
    // config line by hand); every other seat keeps following the universal
    // trim, including live edits to it. The F1 sliders are the same three
    // widgets always — they simply bind to whichever seat is under you.
    float vehicle_cam_forward_v[kVehicleTrimSlots] = {};
    float vehicle_cam_up_v[kVehicleTrimSlots] = {};
    float vehicle_cam_right_v[kVehicleTrimSlots] = {};
    bool vehicle_cam_forward_set[kVehicleTrimSlots] = {};
    bool vehicle_cam_up_set[kVehicleTrimSlots] = {};
    bool vehicle_cam_right_set[kVehicleTrimSlots] = {};
    // ODST's own bank, written as vehicle_cam_*_m_odst_<vehicle>_<seat>. The
    // universal trims above are still the fallback for an unset ODST seat, so
    // one universal nudge continues to move every seat in both titles.
    float odst_vehicle_cam_forward_v[kOdstVehicleTrimSlots] = {};
    float odst_vehicle_cam_up_v[kOdstVehicleTrimSlots] = {};
    float odst_vehicle_cam_right_v[kOdstVehicleTrimSlots] = {};
    bool odst_vehicle_cam_forward_set[kOdstVehicleTrimSlots] = {};
    bool odst_vehicle_cam_up_set[kOdstVehicleTrimSlots] = {};
    bool odst_vehicle_cam_right_set[kOdstVehicleTrimSlots] = {};
    // Reach's own bank, written as vehicle_cam_*_m_reach_<vehicle>_<seat>.
    // The first 20 identities ship a headset-unverified Blender
    // lineup; later census rows remain unset. F1's explicit universal reset is
    // serialized separately so absence of a numeric key continues to mean
    // "use the shipped placement", not "discard it".
    float reach_vehicle_cam_forward_v[kReachVehicleTrimSlots] = {};
    float reach_vehicle_cam_up_v[kReachVehicleTrimSlots] = {};
    float reach_vehicle_cam_right_v[kReachVehicleTrimSlots] = {};
    bool reach_vehicle_cam_forward_set[kReachVehicleTrimSlots] = {};
    bool reach_vehicle_cam_up_set[kReachVehicleTrimSlots] = {};
    bool reach_vehicle_cam_right_set[kReachVehicleTrimSlots] = {};
    bool reach_vehicle_cam_use_universal[kReachVehicleTrimSlots] = {};
    // Optional vehicle-frame view: ON follows ground-vehicle yaw and pitch
    // while keeping roll out of the horizon; aircraft stay yaw-only. OFF keeps
    // the world-locked view Alpha 0.3.1 shipped; it remains the default.
    bool vehicle_view_follow = false;
    // Use the same interpolated seat/attachment node the visible vehicle uses,
    // plus occupant-head motion relative to the settled pose in that seat.
    // 0 = raw node matrices as a diagnostic A/B.
    bool vehicle_cam_smoothing = true;
    // Tell Halo the occupied seat is a FIRST-PERSON seat while the VR vehicle
    // camera owns the view, so the engine stops drawing your own character
    // where your head is. Only the currently occupied loaded seat tag is
    // touched, and the original value is restored on seat exit; the map file
    // is never modified. Ignored entirely when vehicle_first_person = 0, so
    // third-person driving is unaffected.
    bool vehicle_hide_body = true;
    // Hang the first-person arms and gun off the SEAT rather than off Halo's
    // seated camera, which is the occupant's own head marker. ON keeps them
    // riding the vehicle while your head turns freely; OFF restores the
    // head-anchored origin. Only applies inside a first-person vehicle seat.
    bool vehicle_hands_follow_body = true;
    // How much of the seat's occupant bounce reaches the view, 0..1. 1 is the
    // engine's full travel (about 24 cm) and reads as too much in the headset;
    // 0 bolts the view to the seat. This is a strength control, not a filter -
    // no smoothing is applied at any setting.
    // Squadrons: "we never shake the camera, we only shake the cockpit around
    // the camera". v7 0 = the eye stays rigid in the seat; older configs 0.35.
    float vehicle_bounce = 0.0f;
    // Fade to black as the head leaves the cabin: lean this far (m) from the
    // seated eye captured on entry (full black 0.10 m further). 0 = off.
    // v7 0.5; older configs 0.
    float vehicle_cabin_fade_m = 0.5f;
    // Re-neutralise the room-space origin on a settled seat entry AND exit, so
    // physical movement made on foot or in the seat is not carried across the
    // boundary as a standing offset. Position only: it never snaps your facing.
    bool vehicle_recenter_on_seat = true;

    // Hold the engine's automatic exposure steady while a first-person seat
    // owns the view. Looking down at a vehicle's dashboard fills the game's
    // luminance measurement with a big dark surface, so its eye adaptation
    // ramps the whole scene's brightness -- something flat play never provokes
    // because nobody looks at the dash. Uses the engine's own exposure lock,
    // engaged only in the seat, so ordinary play keeps its adaptation and
    // authored cinematic exposure changes still work. ODST for now.
    bool vehicle_steady_exposure = true;
    // ODST drop pod: head look counts as the "look around the pod" input the
    // pod script waits for (5 degrees of head travel each way). OFF = only
    // the stick counts, as in the stock game.
    bool odst_pod_head_look = true;
    // Halo CE opening tutorial: head look counts as the "look around" input
    // its first stage waits for (5 degrees of head travel each way). OFF =
    // only the stick/mouse counts, as in the stock game.
    bool ce_tutorial_head_look = true;
    // Halo 2: the same for any script look test (e.g. the opening's look
    // calibration). OFF = only the stick/mouse counts.
    bool halo2_tutorial_head_look = true;
    // Halo 4: the same for any script look test.
    bool halo4_tutorial_head_look = true;

    // Motion steering in look-steered ground driver seats: grab an invisible
    // wheel with both grips and turn it. 0 = the right stick steers.
    // ALSO the master switch for Halo 2 vehicle aim/view and the CE vehicle
    // turn frame: keep it on. The double-click wheel itself has its own key.
    bool vehicle_motion = true;
    // The double-click-both-grips invisible wheel (Point-mode seats). Default
    // OFF since v13 (headset testing 2026-10-10: it steers by which side of
    // the view the hands are), until wheel v2.
    bool vehicle_double_grip_wheel = false;
    float vehicle_wheel_max_deg = 80.0f;     // wheel lock (Vice City VR)      // wheel angle at full lock
    float vehicle_wheel_deadzone_deg = 2.4f; // 3 % of the 80 deg lock  // slack around centre

    // Halo 4 first-person hands. Places the engine's own
    // first-person weapon assembly at the controller instead of at the head,
    // so the gun and arms follow your hand. The trims are metres, applied in
    // the controller's own frame before the transform reaches the engine, so
    // they read the same way the vehicle seat trims do.
    bool halo4_hands = true;
    float halo4_hand_forward_m = 0.00f;  // + pushes the gun away from you
    float halo4_hand_vertical_m = 0.00f; // + raises it
    float halo4_hand_lateral_m = 0.00f;  // + moves it toward your right
    // Left-handed: mirrors the placement across the forward axis.
    bool halo4_hands_mirrored = false;
    // Halo 4's authored Mjolnir helmet/visor overlay. This is deliberately
    // separate from HUD scale/curvature so players can keep the complete HUD
    // while hiding only the helmet frame.
    bool halo4_helmet = true;
    // Halo 4 helmet frame size: 0 = auto (1.6x HUD size) unless halo4_visor_follow_hud, else
    // 0.30..1.00 drawn at its own size while the HUD keeps hud_size.
    float halo4_visor_size = 0.0f;
    // true = the frame shrinks with the HUD (pre-v7 behaviour); false = it keeps
    // its own size (halo4_visor_size, 0 = auto 1.6x HUD).
    bool halo4_visor_follow_hud = false;
    bool hide_hud = false;
    bool hud_reveal_near_head = false;
    float hud_reveal_radius_m = .22f;
    // Per-element HUD placement (Reach). Off: the whole-HUD settings only.
    // Indexed by hud_element::Element; see hud_element_logic.h.
    bool hud_per_element = false;
    // One row per game (hud_element::PlacementSet): tuning one game never moves
    // another. Reach/Halo 2/CE rows are per Element, Halo 3/ODST per Slot.
    hud_element::PlacementRow hud_place[hud_element::SetCount]{};
    // Immersive (v7): touch the empty off hand to the head to toggle the
    // flashlight / VISR (the earlier CE VR mod's gesture). Off for v6-and-older configs.
    bool flashlight_head_gesture = true;
    // Optional CE Anniversary flare workaround; scene lighting stays native.
    bool ce_anniversary_disable_lens_flares = false;
    // Optional per-controller firing for an owned native dual-wield pair.
    bool independent_dual_aim = false;
    bool gun_barrel_aim = true;

    // rendering. The game's own HUD reticle sits at head-center and is wrong
    // whenever hand aim is on; this one is the truth.
    bool crosshair = true;
    float crosshair_distance_m = 41.0f; // how far along the aim ray it floats
    float crosshair_size_deg = 10.10f;  // apparent (angular) size
    // Independent dual-wield reticles (Halo 3, ODST, Halo 2): multiplier on the
    // crosshair size so two glyphs stay small and thin (headset feedback:
    // "comically large, thick, cover too much"). v7 0.25; older configs 1.0.
    float dual_reticle_scale = 0.25f;

    // How often the VR crosshair re-reads the game's own animated crosshair
    // art, in displayed frames. Halo 3's authored crosshair kicks when the
    // weapon fires and turns red on a hostile / green on a friendly, so a
    // crosshair that is read once and held looks frozen. Lower = more
    // responsive, higher = cheaper. 0 holds one image and never animates,
    // which is the cheapest setting. Halo 3 only; ODST and Reach keep their
    // own proven cadence. Live in the F1 menu.
    int crosshair_animation_frames = 6;

    // Crosshair color (0-1 per channel). Default approximates Halo 3's own
    // light CHUD blue. File-only: there is no F1 widget for these.
    float reticle_r = 0.62f;
    float reticle_g = 0.87f;
    float reticle_b = 1.0f;

    // Hand-anchored first-person weapon: uniform size multiplier applied to
    // the RIGHT wrist subtree (hand + weapon) around the wrist. Under the true
    // world projection the authored viewmodels read oversized. 0.96 is the
    // headset-tuned release default. Home/End adjust live.
    float gun_scale = 0.96f;

    // Same trim for the LEFT wrist subtree: the support hand, and the second
    // gun when dual-wielding. Independent of gun_scale because the left hand
    // holds no weapon most of the time. 1.00 = authored size, which is what
    // the left hand has always rendered at — until 2026-07-20 the trim loop
    // used the RIGHT wrist's bone mask for both sides, so no left-hand scale
    // value ever reached a bone.
    float left_hand_scale = 0.96f;
    // Visible hand meshes only, in each hand's local frame (metres).
    float left_hand_mesh_x_m = 0.0f;
    float left_hand_mesh_y_m = 0.0f;
    float left_hand_mesh_z_m = 0.0f;
    float right_hand_mesh_x_m = 0.0f;
    float right_hand_mesh_y_m = 0.0f;
    float right_hand_mesh_z_m = 0.0f;

    // (gun_length_scale removed 2026-07-19: a barrel-only squash is not
    // expressible in the engine's uniform-scale bone format; moving bone
    // origins just translated the rigid gun mesh.)

    // Shared controller aim calibration (degrees). ComputeAimPose applies it
    // to the aim ray, reticle and weapon together. Keep the historical keys
    // and saved calibration; use barrel_* below for visual-only alignment.
    float gun_pitch_deg = -3.0f;
    float gun_yaw_deg = 0.0f;
    float gun_roll_deg = 0.0f;

    // Mesh-only barrel trim (degrees): rotates the visible gun and the hands
    // holding it about the controller, leaving the crosshair and the shot
    // ray exactly where they are. Use it to lay the drawn barrel on the
    // crosshair line. Saved per game (see title_profiles). 0 = untouched.
    float barrel_pitch_deg = 0.0f;
    float barrel_yaw_deg = 0.0f;
    float barrel_roll_deg = 0.0f;

    // Push the whole arms+gun assembly along the controller's forward axis,
    // in meters. 0 = anchored at the controller; negative seats the gun back
    // into/behind your fist (the practical "gun feels too long" trim);
    // positive moves it out of your face. Never touches aim.
    float gun_forward_m = -0.14f;

    // Gun-stock calibration: lateral/vertical translation along the visible
    // weapon's own post-mount-rotation right/up axes. Shared by every supported
    // VR title. Visual only: aim, bullets, and the VR reticle stay on the
    // controller ray. 0 = previous placement.
    float gun_right_m = 0.0f;
    float gun_up_m = 0.0f;

    // Halo 2 CLASSIC ONLY: extra rotation and offset applied to the visible
    // gun AND hands together, so Classic can be lined up with what
    // Anniversary shows. Anniversary is never touched by these; every other
    // title ignores them. The Halo 2 Classic baseline is yaw +1.0 and pitch
    // -9.5; saved config values still win and remain LIVE-editable in F1.
    // Switch to Anniversary to compare without applying this extra trim.
    // Degrees, about the held weapon's own axes (pitch raises/lowers the
    // muzzle), then meters along its own forward/right/up.
    float halo2_classic_gun_pitch_deg = -9.5f;
    float halo2_classic_gun_yaw_deg = 1.0f;
    float halo2_classic_gun_roll_deg = 0.0f;
    float halo2_classic_gun_forward_m = 0.0f;
    float halo2_classic_gun_right_m = 0.0f;
    float halo2_classic_gun_up_m = 0.0f;

    // Raise the muzzle EFFECT origin — the flash and the point bullets appear
    // to leave — along the gun's own up axis, in meters. Reach only; Halo 3 and
    // ODST resolve their muzzle markers on the visible weapon already.
    //
    // Reach's effect markers resolve against the stock, head-anchored weapon,
    // so the mod re-parents them onto the controller-driven gun. That transfer
    // keeps the authored marker offset in the gun's own frame exactly,
    // which lands the origin on the barrel line but at the authored height —
    // reported in-headset as sitting a few inches low. This trims that, and
    // ONLY that: it is applied after the projectile's own origin and direction
    // are resolved, so where rounds actually land is untouched.
    //
    // 0 = the authored marker height (previous behavior). Positive = up along
    // the barrel, so it rolls with the gun instead of staying world-vertical.
    // ~0.11 is the reported "four or five inches". Tune LIVE in the F1 menu.
    float muzzle_height_m = 0.0f;
    // Independent title preferences. H2 Original/H4 retain the existing
    // suppression by default; other titles need their own proven hide paths.
    bool hide_muzzle_flash[6]{false, false, false, true, false, true};
    // Per-game stock bloom. Verified optional suppression: H3, ODST, Reach.
    bool bloom_enabled[6]{true, true, true, true, true, true};

    // Experimental gun-mounted VR zoom screen. R3 is isolated from Halo's
    // native zoom so the full VR gun/body remain visible; scope_zoom is the
    // fixed 4:3 lens restored on every activation before right-stick adjustment.
    bool scope_enabled = true;
    float scope_zoom = 24.0f;
    float scope_screen_width_m = 0.159f;
    float scope_screen_right_m = -0.058f;
    float scope_screen_up_m = 0.216f;
    float scope_screen_forward_m = 0.050f;
    // The zoom camera sits at the gun's muzzle plus this much along the
    // barrel (the earlier CE VR mod's placement), not at the head, so the rifle never
    // blocks the view. Per title: H3, ODST, Reach, H2, H4 (CE: ce_scope_forward_m).
    bool scope_camera_at_muzzle = true;
    // A gun with scope glass (Optic kind) shows the zoom on its
    // glass the whole time it is held; nothing engages or releases and R3 does
    // nothing for it. The look gate only sets the render rate: full rate while
    // raised to the eye, every scope_idle_cadence frames otherwise (always live,
    // just cheaper). 0 = the old look/button engage (scope_activation).
    // Helmet zoom (Visor kind, e.g. the Reach magnum) keeps visor_zoom_* and R3.
    bool scope_live_glass = true;
    int scope_idle_cadence = 4;
    // While the scope pass renders and recent frames miss the headset's frame
    // budget, the pass renders at half width and height (a quarter of the pixels), back
    // to full size once the frames recover (held; scope_render_scale_logic.h). Titles
    // opt in in vr.cpp (ScopeAdaptiveTitle: Halo 3). 0 = always full size.
    bool scope_adaptive_resolution = true;
    // Reach "lower quality while scoped": the scope pass widens the head
    // camera's FOV for shared culling. 0 = as before; 1 = cull only (the eye
    // passes get the real head FOV back); 2 = cap the widening at
    // reach_scope_cull_cap_deg (unproven, so not a default).
    // Headset feedback ("insanely lower graphics quality" while zoomed): mode 1 is the
    // default since v20 - culling keeps the widened FOV, the eye passes render
    // with the real head FOV (mode 0 let them use the widened one: low LOD).
    int reach_scope_cull_mode = 1;
    // Reach scope pass order: 0 = before the eyes, 1 = after the eyes (default since
    // v22). The first player_view_render of a frame submits Reach's instance
    // occlusion queries (0x26D740 -> 0x293400) with its own camera, and the next
    // frame's visibility (0x293AB4) culls whatever those queries saw no pixels of.
    // Scope first let the gun's narrow view cull the eyes' periphery.
    int reach_scope_order = 1;
    // Diagnostic (never in F1): log the Reach muzzle/lens receipt builder's stages ("Reach muzzle trace").
    bool reach_muzzle_trace = false;
    float reach_scope_cull_cap_deg = 20.0f;
    float scope_camera_forward_m[5]{0.05f, 0.05f, 0.05f, 0.05f, 0.05f};
    // Lens-node path (Reach/ODST/H4): most the camera moves from the lens toward
    // the gun's front (spec: camera at the lens). 0 = stay at the lens.
    // v21 (headset feedback, Reach: the barrel end showed in the zoom picture): 2.0 m, so the
    // camera clears each gun's own model front (Reach sniper 1.26 m); was 0.30.
    float scope_lens_forward_max_m = 2.0f;
    // Look-through activation: 0 = R3 only (old), 1 = look only, 2 = both.
    int scope_activation = 2;
    // 2026-10-09: 0.12 m / 12 deg was unreachable in practice (the lens point is
    // 0.216 m above the grip). v7 0.25 m / 25 deg; older configs keep 0.12 / 12.
    // 2026-10-10: realistic look-to-scope distance, 15-20 cm from the lens and a
    // ~10 degree cone (was 0.25 m / 25 degrees, which fired too early). v7 0.18 /
    // 10; older configs 0.12 / 12. A v7 file that still has the previous 0.25 /
    // 25 pair and no scope_look_defaults key is upgraded once on load.
    // Look-to-scope (v10, headset feedback): gun raised level (pitch within
    // scope_sight_angle_deg of the horizon) with the scope between
    // scope_sight_min_distance_m and scope_sight_distance_m from the eye.
    float scope_sight_distance_m = 0.30f;     // farthest eye-to-lens (11 in with margin)
    float scope_sight_min_distance_m = 0.15f; // nearest eye-to-lens
    float scope_sight_angle_deg = 15.0f;      // gun pitch from level, either way
    // Halo CE visor crop: cap on the magnification it shows (2 = the crop stays
    // sharp enough; 99 = uncapped). v7 2.0; older configs uncapped.
    float ce_visor_zoom_max = 2.0f;
    int scope_eye = 0; // 0 weapon side, 1 left, 2 right
    // Driving: steering per vehicle class, 0 Point (hand points),
    // 1 Wheel (grab a virtual wheel), 2 Stick. New v7 configs: Wheel; v6-and
    // older keep Point. Wheel/Stick make that driver seat follow the hull.
    // From 2026-10-09 prior art: v7 default Stick (Halo's own steering; the grab
    // Wheel stays an opt-in experiment). Vehicle guns aim with the head, or
    // the hands with turret handles.
    // v13 (headset testing 2026-10-10, grab wheel steers by which side of the
    // view the hand is): saved Wheel (1) moves to Stick (2) once, until wheel v2.
    int vehicle_steer_car = 2;  // Warthog, Prowler, trucks, Revenant
    int vehicle_steer_bike = 2; // Mongoose, Ghost, Chopper
    float vehicle_wheel_radius_m = 0.18f;
    float vehicle_wheel_forward_m = 0.40f;
    float vehicle_wheel_down_m = 0.35f;
    float vehicle_exit_hold_ms = 800.0f;   // grip held out at the side (hip/door) exits
    int vehicle_wheel_feel = 0;            // 0 turn rate (as before), 1 direct angle (experimental)
    bool vehicle_wheel_v2 = true;          // grab wheel turns by hand ROTATION about its axis (not hand position)
    float vehicle_wheel_ratio_deg = 45.0f; // direct angle: look offset from the nose at full lock
    // Comfort: new v7 configs on, older configs off.
    float vehicle_vignette = 0.6f;    // turn vignette strength, 0 = off
    bool vehicle_horizon_lock = true; // following seats turn with the vehicle but never pitch
    // Enclosed vehicles (Wraith, H2 Scorpion/Wraith): lift the eye toward the
    // hatch by default so the driver can see out. v7 on; older configs off.
    // Headset testing 2026-10-10: no eye-up seats. Kept as an opt-in (default off).
    bool vehicle_cockpit_eye_up = false;
    // Enclosed vehicles (Reach Scorpion/Wraith/Banshee/Seraph first): a framed
    // screen in the cockpit shows the view from a forward camera on the hull
    // (a second render through the scope pass). v7 on; older configs off.
    bool vehicle_cockpit_screen = true;
    float cockpit_screen_width_m = 0.60f;
    float cockpit_screen_distance_m = 0.45f; // kit: cockpit walls are 0.47-0.63 m ahead of the eye
    float cockpit_screen_offset_y_m = 0.0f;
    float cockpit_screen_fov_deg = 90.0f;
    float cockpit_screen_ahead_m = 0.0f;    // hull camera ahead of the seat; 0 = per class
    int cockpit_screen_refresh_divisor = 2; // render every Nth frame (GPU cost)
    // Vehicle monitors: with the cockpit
    // screen on, three screens (forward, left, right hull cameras), one view
    // rendered per frame in turn. Off = the single screen above.
    bool vehicle_monitors = true;
    bool vehicle_monitor_forward = true, vehicle_monitor_left = true, vehicle_monitor_right = true;
    float vehicle_monitor_width_m = 0.42f;
    float vehicle_monitor_distance_m = 0.60f;
    // Horizon OFF (pitch follows): hold the horizon level for this much hull
    // roll, then blend into the roll by twice it. v7 6; older configs 180 (never roll).
    float vehicle_horizon_hold_deg = 6.0f;
    // On-foot smooth-turn vignette strength (0 = off). v7 0.5; older configs 0.
    float turn_vignette = 0.5f;
    bool vehicle_dash_cue = true; // faint vehicle-fixed line ahead in following seats
    // Turret handles: grab a two-grip handle bar in gunner seats. New v7
    // configs on, older off.
    bool vehicle_turret_handles = true;
    bool turret_handle_seat_axis = true; // handles centre on the seat axis captured at entry, not the head yaw
    bool turret_handle_lever = true;     // true: spade-grip lever; false: aim where the handles point
    float turret_handle_forward_m = 0.40f;
    float turret_handle_down_m = 0.30f;
    float turret_handle_span_m = 0.24f;
    // Turret gyro (turret_gyro_logic.h): mounted guns hold their world aim
    // while the hull turns and bumps (hull rate fed forward into the aim stick).
    bool vehicle_turret_gyro = true;
    float turret_gyro_strength = 1.0f;   // 0 off .. 2
    float turret_gyro_rate_scale = 1.0f; // scales the per-gun full-stick turn rates (0.25..4)
    // Flight stick: grab a virtual stick with the weapon hand in aircraft
    // driver seats. New v7 configs on, older off.
    bool vehicle_flight_stick = true;
    bool flight_stick_invert = false;    // true: push forward climbs
    float flight_stick_tilt_deg = 25.0f; // shaft lean forward
    float flight_stick_full_deg = 30.0f; // tilt for full stick
    float flight_stick_forward_m = 0.30f;
    float flight_stick_down_m = 0.45f;
    // Visor zoom (weapons without a sight): head-locked HUD window.
    int visor_zoom_mode = 1; // 0 off, 1 by weapon, 2 every weapon
    // Halo CE has no second zoom render yet: its visor zoom is a crop of the
    // headset eye image around the aim point (2-4x), shown in the visor window.
    bool ce_visor_zoom = true;
    // Halo CE Classic graphics: a real look-through scope for weapons with a
    // sight (Optic: sniper rifle, rocket launcher) - one extra render of the
    // world per zoomed frame (method from the earlier CE VR mod's PiP scope). Anniversary
    // graphics keep the visor crop.
    bool ce_classic_scope = true;
    // How far ahead of the aiming hand the CE scope camera sits (in front of
    // the hand-held gun, like the earlier CE VR mod's muzzle-placed scope camera).
    float ce_scope_forward_m = 0.5f;
    // When the gun's kit glass is known (CE sniper), the
    // Classic scope camera starts at the glass and sits this far past it
    // (clears the gun's own scope body); ce_scope_forward_m is then unused.
    float ce_scope_lens_clearance_m = 0.12f;
    // Headset feedback: the CE sniper glass "was and always has been a perfect square",
    // "tall enough and too thin. It needs to be a longer rectangle"): width/height of
    // the non-square CE scope glass (the sniper's). The height stays the square's side
    // (40 mm); later headset feedback asked for "about 3x the height": 3.0 = 120 x 40 mm. Range 1.0 (square) to 4.0.
    float ce_sniper_glass_aspect = 3.0f;
    // Since v25 the helmet-zoom window is four signed edges in the
    // window plane (metres from the eye-forward axis, +x right, +y up), grown
    // by visor_zoom_scale about its centre, at visor_zoom_distance_m. The
    // zoomed picture is drawn at the anchor (cfg-only), and the edges only mask
    // it (visor_zoom_logic.h). Defaults: today's 0.20 m 4:3 window at
    // (0.032, -0.020). Per game through the per-title profiles.
    float visor_zoom_left_m = kVisorDefaultLeftM;
    float visor_zoom_right_m = kVisorDefaultRightM;
    float visor_zoom_top_m = kVisorDefaultTopM;
    float visor_zoom_bottom_m = kVisorDefaultBottomM;
    float visor_zoom_anchor_x_m = 0.032f;
    float visor_zoom_anchor_y_m = -0.02f;
    // Before v25: read only to migrate (width x scale, centred on the offsets).
    float visor_zoom_width_m = 0.133f;
    float visor_zoom_scale = 1.0f; // window size multiplier about its centre (reticle keeps its size)
    // Halo CE: the visor window shows its own full-resolution zoom render (third pass, head view)
    // instead of a stretched crop of the eye image. Costs one extra world render per zoomed frame.
    bool ce_visor_fullres = true;
    float visor_zoom_distance_m = 0.40f;  // 0.25-2.00 m (v25 lowered the floor from 0.40)
    float visor_zoom_offset_x_m = 0.032f; // before v25 only: the window centre (right-eye axis)
    float visor_zoom_offset_y_m = -0.02f;
    float visor_frame_opacity = 0.85f;
    float visor_raise_distance_m = 0.30f; // raise-to-eye, relaxed vs the lens
    float visor_raise_angle_deg = 20.0f;
    // Zoomed views (lens and visor): extra aim smoothing by zoom level
    // (0.4 / 0.6 in the earlier CE VR mod; 0 off, 2 max) and a level horizon.
    float zoom_smoothing_low = 0.4f;  // at the lowest zoom
    float zoom_smoothing_high = 0.6f; // at the highest zoom
    bool zoom_level_horizon = true;
    float zoom_edge_softness = 0.40f; // feathered zoom edges (visor full, lens gentle)
    int scope_refresh_divisor = 1;    // legacy key; lens now refreshes every frame

    // (show_hud / hud_ammo / hud_health / hud_motion / hud_grenades retired
    // 2026-07-19 evening: their chud+0x144..0x14A byte writes used a
    // headset-disproven offset map and suppressed the whole HUD except the
    // objective text. The native HUD is fully game-managed now; the only
    // element control is the reticle kill below.)

    // Hide every native CHUD widget collection marked scripting class
    // crosshair. The head-locked game cursor is redundant with our VR reticle.
    bool kill_reticle = true;

    // Game brightness / gamma, applied to the screen colour constant every title
    // publishes: halo3+0x278EE0, halo3odst+0x2A6308, haloreach+0x252E28. 1.0 =
    // the game's own brightness; higher = brighter, lower = darker. NOT a HUD
    // control — the function once thought to size the HUD adjusts brightness.
    // One value for all three titles, so a change is felt everywhere.
    float game_brightness = 1.11f;

    // Halo's internal raster scale, applied by the launcher on the next game
    // start. ANY value from kResolutionScaleMin to kResolutionScaleMax is
    // honored exactly; the named tiers (potato .50, low .75, medium 1.00,
    // high 1.30, ultra 1.80, keith david 2.64 = 8K width) are only F1 shortcuts.
    // The OpenXR projection remains at the headset's full size.
    float resolution_scale = 1.0f;

    // Fit the desktop game window to your monitor while the HEADSET keeps
    // rendering at the full resolution_scale size. On a monitor smaller than the
    // render (e.g. an 8K render on a 1080p panel), MCC's window overflows the
    // screen and its menu buttons ("Halo 3", Quit) land off the edge where the
    // mouse can't reach them. With this on, MCC still draws the full-size frame
    // (so the headset picture and the gun alignment never change) but the visible
    // window is shrunk to fit and the GPU downscales the picture into it for free
    // -- no extra render pass, no measurable cost. Ships ON; when
    // disabled the desktop window behaves exactly as it always has. Like
    // resolution_scale, this takes effect on the next game start.
    bool fit_desktop_window = true;

    // The VR frame is submitted inside MCC's desktop Present, so MCC's own
    // V-Sync would pace the HEADSET at the DESKTOP monitor's refresh -- a 60 Hz
    // desktop capping a 120 Hz headset. With this on we present the desktop
    // mirror unlocked and let the OpenXR runtime's reported display period be
    // the only clock, so 72/90/120/144 Hz headsets each pace themselves. The
    // desktop mirror may tear; the headset never does (the compositor owns it).
    // ON by default -- the headset must never inherit the desktop's refresh.
    bool desktop_present_unlocked = true;

    // Halo 2: the Menu/Start button also shows the pause menu in VR (head-
    // locked 2D) once the game has actually paused (its game-time clock
    // stops). 0 = only the Y+B chord switches H2 to the 2D pause screen.
    bool halo2_start_pause = true;

    // Halo Reach crash guard (internal compatibility fix, no F1 control):
    // skip the action-test builder when its state is not allocated (the
    // haloreach+0x65BA9 crash). Installs once a level runs.
    bool reach_action_test_guard = true;
    // While Reach loads, MCC gets a connected but neutral pad (no buttons or
    // sticks) until the level runs. Diagnostic for early input reaching the
    // title mid-load; internal fix, no F1 control.
    bool reach_neutral_pad_while_loading = true;

    // Halo Reach obeys MCC's frame-rate limit (often 60) inside the game
    // itself, which pins a 90/120 Hz headset at 60 fps and judders on head
    // motion. On: Reach runs uncapped (MCC's own "unlimited" values, left in
    // place), in and out of VR; the headset paces it in VR. Off: no write.
    bool reach_uncap_framerate = true;
    // The same for Halo 4 (Reach's option setter, cap/target pair) and
    // Halo 2 (its option table maps every choice to MCC's "unlimited"): the headset
    // paces them instead of their stored 60 fps limit. Off: no write (Halo 2's option
    // table gets its stock bytes back, and its own poller restores MCC's limit).
    // Also Halo 3 and ODST (Reach's cap/target pair in halo3/halo3odst.dll) and
    // Halo 2's second limiter (its own frame time + flag; restored when switched off).
    bool titles_uncap_framerate = true;

    // Dormant diagnostic: measure how long the mod holds MCC's render thread
    // inside Present and log the run-up whenever a level is torn down. Keep the
    // config surface and evidence implementation for targeted future runs; the
    // production Present path is compile-time disabled in d3d11_hook.cpp.
    bool coop_probe = false;

    // Image quality (mod-owned, applied live when each eye is expanded into the
    // headset — universal to every title, no restart). These replace/augment the
    // plain linear upscale that made edges shimmer even at high resolution.
    //
    // Upscale/resolve filter: 0 = linear (the old behavior), 1 = sharp
    // (Keys bicubic, a=-0.75). Linear is the default since v14: a grain measurement found
    // the sharp filter adding high-pass noise (Reach shimmer) on top of RCAS.
    int upscale_filter = 0;

    // RCAS-based 2x-overdrive sharpening, 0.00 = off, 1.00 = max. Same five
    // texture loads/one pass; the intentionally aggressive top can ring or clip.
    float sharpness = 0.0f; // off by default since v14 (grain measurement)
    // Noise gate for the sharpening: local contrast (green channel, 0..1) below this is left unsharpened (grain), ramping to
    // full strength at twice the value. 0 = off (sharpen everywhere). Reach crispness candidate; try 0.03-0.08.
    float sharpness_noise_gate = 0.0f;

    // Anti-aliasing on the finished eye: 0 off, 1 FXAA, 2 FXAA Strong, 3 genuine
    // SMAA 1x, 4 SMAA 1x followed by FXAA Strong. SMAA modes are optional and
    // heavier; Off/FXAA do not run their passes or retain the third eye target.
    int aa_mode = 0;
    // Size the eye swapchain so the submitted native-FOV rect matches the
    // headset's recommended resolution (sharper; more GPU for the eye resample).
    bool eye_full_density = true;

    // Render draw-distance trim, applied live to every title's shared
    // render_far_clip_distance debug var (stock 10240 world units), resolved by
    // name — no hardcoded addresses. 1.00 = full stock draw distance (no
    // change); lower culls distant geometry earlier for CPU headroom in stereo.
    // Halo 3, ODST, and Reach all honor it. Takes effect immediately, no restart.
    float draw_distance = 1.0f;

    // Weather, off by default because both effects read as a soft, washed-out
    // image in a headset. Each drives the engine's own switch for that effect,
    // asserted at the top of every VR eye render; turning a setting back on
    // restores the title's stock value live, with no restart.
    //
    // Universal settings, like every key here, but only Halo: Reach binds them
    // today - that is where the effects were reported and where the evidence is
    // (HREK's own "Fog and Weather" debug menu plus the pinned haloreach.dll
    // gates; see src/common/titles/reach/reach_render_logic.h). A title with no proven
    // binding stays stock for that effect and says so in the log, exactly as an
    // unresolved draw-distance var does.
    //
    // Rain drives Reach's `render_rain` boolean, resolved by name. Clearing it
    // gates out the rain particle renderer that draws streaks across the view.
    bool rain = false;
    // Atmospheric fog drives bit 0x04 of Reach's render flags - the exact bit
    // HREK's `render_atmosphere_fog` command writes. Clearing it skips the
    // atmosphere fog upload, which is the distance haze that flattens contrast
    // on far terrain. This is NOT the screen-aligned patchy fog, which the mod
    // has always suppressed per eye.
    bool atmospheric_fog = false;

    // (The 0x2EEFC8 placement-slider experiment is retired: measured 2026-07-19,
    // that struct holds colors/alpha/animation only — Halo's HUD has no position
    // data to edit. The HUD panel below is the real fix.)

    // (HUD sizing experiments before 2026-07-19 PM are retired: the capture-diff
    // panel was headset-disproven and the hud_zoom [view+0x2B0]+0x174 poke never
    // resized anything. hud_size below is the one that works — it is DATA, not
    // code: Halo's own layout input.)

    // HUD size: the fraction of the view the HUD lays out into. This drives
    // Halo's own "global safe frame" floats inside the loaded chud_globals tag
    // data (located at runtime by their immutable byte neighborhood; proven in
    // H3EK tag_test on desktop AND live in MCC, 2026-07-19 probe: the engine
    // re-lays the HUD out the same frame the floats change). 0.87 = the game's
    // stock value (mod applies nothing); smaller pulls shields/radar/ammo
    // toward the screen center where both VR eyes can see them.
    float hud_size = 0.43f;

    // Extra horizontal trim after the runtime headset-aspect correction.
    // 1 = automatic shape, lower = narrower, higher = wider.
    float hud_aspect = 1.22f;

    // Normalized curvature: 0 = flat (+0.30 destination-Z delta), 1 = fully
    // curved (-0.30 delta), and 0.5 retains each HUD skin's authored value.
    float hud_curvature = 0.48f;

    // Vertical HUD translation in Halo virtual-screen pixels. Positive raises
    // the complete HUD and negative lowers it; the authored reticle is excluded
    // because VR renders it separately on the controller aim ray.
    float hud_vertical_offset = 16.0f;

    // Automatically enter VR (head tracking + stereo) when a level loads, and
    // drop back to the flat menu screen when you leave — no F2/F11 needed.
    bool auto_vr = true;

    // Main weapon/aim/trigger use the physical left controller. Movement,
    // turning and face buttons keep their existing physical bindings.
    bool left_handed = false;
    // Preserve released controller-role positioning unless explicitly opted in.
    bool experimental_hand_alignment = false;

    // Optional body gestures; each title keeps its own MCC controller layout.
    bool manual_reload = false;
    bool manual_reload_disable_auto = false;
    bool manual_reload_skip_animations = false;
    bool manual_reload_shortened_animation = false;
    bool weapon_holsters = true;
    float weapon_pouch_down_m = 0.50f;
    int weapon_pouch_location = 0; // 0 support-side front hip, 1 behind support shoulder
    float weapon_pouch_offset_x_m = 0.0f;
    float weapon_pouch_offset_y_m = 0.0f;
    float weapon_pouch_offset_z_m = 0.0f;
    float weapon_body_zone_radius_m = 0.20f;
    float weapon_holster_radius_m = 0.20f;
    float weapon_insert_radius_m = 0.13f; // snap-in reload seats on entering this radius of the magazine well
    float weapon_holster_draw_m = 0.25f;
    bool weapon_holster_slide = true;
    bool weapon_holster_click = true;     // a grip squeeze in the zone swaps at once ("tap the shoulder")
    bool weapon_zones_body_anchor = true; // holster/pouch follow a smoothed body yaw, not every head turn
    bool weapon_reload_take_grip = false; // false: take the magazine with the support trigger; true: the grip (old)
    // Off-hand grenade throw (off until headset-tested): support trigger in the
    // chest slot takes a grenade, a fast release throws, a slow one puts it back.
    bool weapon_grenade_throw = false;
    // Immersive (v7) body slots: grab at the support-side back hip to switch
    // grenade type; Halo 3 equipment from the front belt, toss to deploy.
    bool weapon_grenade_type_slot = true;
    // Reach grab pickup (single player): lower the support hand to a weapon and
    // squeeze the grip; the game's own Use picks it up. Off by default since
    // 2026-10-23: with an empty hand the engine draws no arms, so the player
    // cannot see what to grab (docs/VR-BODY-RESEARCH.md).
    bool reach_grab_pickup = false;
    // Reach grip-held weapons (single player, on foot; docs/GRAB-G1-PLAN.md 9):
    // the gun hand holds its grip; opening it outside the holsters drops the
    // gun, in a free holster stows it (empty hand), and a grip at a holster draws.
    bool reach_grip_held = false;
    int reach_grip_release_ms = 150; // open this long before a drop or stow (100..400)
    // A third weapon in the holsters. Off until a three-weapon save is proven on
    // vanilla MCC (Reviewer condition 3; GRAB-G1-PLAN.md 9.6).
    bool reach_third_weapon = false;
    // RIG ONLY, F1-hidden, not written back: the build 32 holster probe script
    // (0 off; 1 stow/draw/drop, 2 third weapon and the 15 m survey, 3 busy refusal).
    // docs/GRAB-G1-PLAN.md 7.6 and 8.
    int reach_grab_probe = 0;
    // RIG ONLY, F1-hidden, not written back: the grab gate, grip and hand role
    // lines without a probe script (build 33 rig findings 3 and 4).
    bool reach_grab_rig_log = false;
    // MCC's gameplay input can stay inactive after start-up (its window missed
    // the activation); re-send focus to it. Internal fix, no F1 control.
    bool mcc_input_focus = true;
    // Pad-first menus: while VR is on, MCC never switches its UI to
    // keyboard/mouse mode (the menu laser moves the mouse); the pad's A/B/stick
    // always navigates and the laser stays a layer on top.
    bool mcc_pad_first_menus = true;
    float reach_grab_radius_m = 0.30f; // weapon centre within this of the hand
    float reach_grab_drop_m = 0.35f;   // hand at least this far below the head
    bool weapon_equipment_slot = true;
    float weapon_grenade_slot_radius_m = 0.12f;
    float weapon_grenade_slot_down_m = 0.25f;
    float weapon_grenade_throw_speed = 1.8f;
    float weapon_grenade_haptic = 0.25f;
    // Master for every gesture/contact feedback cue (feedback::kCues); 1 = tuned feel.
    float feedback_cue_strength = 1.0f;
    bool weapon_holster_by_class =
        true; // stowed one-handed gun -> hip, long gun -> shoulder; off = weapon_holster_location
    float weapon_holster_haptic = 0.12f; // holster-entry tick strength, 0 = off
    // Holster tick re-trigger dead zone (as a 2025 VR shooter update does): after a tick, the hand
    // must leave this far past the zone edge (or stay out 300 ms) to tick again.
    // v7 0.05; older configs 0 (any exit re-arms, as before).
    float weapon_holster_haptic_rearm_m = 0.05f;
    bool weapon_needler_shake = false;
    bool weapon_unknown_reload_visual = true;
    float weapon_shake_travel_m = 0.10f;
    int weapon_holster_location = 0; // 0 shoulder, 1 hip
    // X, RB, LB, B, Y, A, LT, RT. H3, ODST, Reach, H4, CE, H2.
    int weapon_reload_button[6]{0, 0, 0, 0, 0, 0};
    int weapon_switch_button[6]{4, 4, 4, 4, 4, 4};

    // Two-handed weapon aiming: with the support hand on the gun front, aim
    // along the two-hand line instead of the primary wrist alone. Engages by
    // support-grip acquisition in the barrel grab zone: Toggle mode (default)
    // flips engaged on/off per grip click, Hold mode engages only while the
    // grip is held. The primary grip still cycles grenades.
    bool two_handed_aim = true;
    // Coherent committed-sample aim (A+B, runtime-validated) is the default.
    // Configs saved before v6 with this key off are migrated on once. In a v6
    // config, set two_hand_coherent_aim = 0 for the hidden legacy rollback/debug
    // escape; the F1 control is retired.
    bool two_hand_coherent_aim = true;
    // Virtual stock: with two-hand engaged, the base aim orientation follows
    // the configured rear-reference blend toward the raw support controller.
    // OFF preserves the released behavior. Changes only the base engaged
    // two-hand orientation; base position/roll stay primary-controller owned
    // and grip acquisition/latching are unchanged. Default off.
    bool virtual_stock = kVirtualStockEnabledDefault;
    // Standard and Plus remember independent rear-reference influence. The
    // active mode supplies one effective value to the existing solver seam.
    float virtual_stock_standard_strength = kVirtualStockV7StrengthDefault;
    float virtual_stock_plus_strength = kVirtualStockV7StrengthDefault;
    // Gravity-aligned OpenXR LOCAL-space height added to the head target.
    float virtual_stock_rear_height_m = kVirtualStockRearHeightDefaultM;
    // Rear reference selector: 0 = Head, 1 = Shoulder, 2 = Chest,
    // 3 = Hybrid.
    int virtual_stock_rear_reference = kVirtualStockProductRearReferenceDefault;
    float virtual_stock_shoulder_back_m = kVirtualStockShoulderBackDefaultM;
    float virtual_stock_shoulder_side_m = kVirtualStockShoulderSideDefaultM;
    float virtual_stock_chest_height_m = kVirtualStockChestHeightDefaultM;
    float virtual_stock_chest_back_m = kVirtualStockChestBackDefaultM;
    float virtual_stock_chest_side_m = kVirtualStockChestSideDefaultM;
    float virtual_stock_adaptive_top_height_m = kVirtualStockAdaptiveTopHeightDefaultM;
    float virtual_stock_adaptive_bottom_height_m = kVirtualStockAdaptiveBottomHeightDefaultM;
    float virtual_stock_adaptive_top_half_width_m = kVirtualStockAdaptiveTopHalfWidthDefaultM;
    float virtual_stock_adaptive_bottom_half_width_m = kVirtualStockAdaptiveBottomHalfWidthDefaultM;
    // Hybrid mode-3 direction authority and finite-segment seating controls.
    float virtual_stock_hybrid_offhand_influence = kVirtualStockHybridOffhandInfluenceDefault;
    int virtual_stock_hybrid_ads_reference = kVirtualStockHybridAdsReferenceDefault; // 0 Head, 1 Shoulder.
    float virtual_stock_hybrid_seat_full_m = kVirtualStockProductHybridSeatFullDefaultM;
    float virtual_stock_hybrid_seat_release_m = kVirtualStockProductHybridSeatReleaseDefaultM;
    bool virtual_stock_hybrid_horizontal_release = kVirtualStockProductHybridHorizontalReleaseDefault;
    float virtual_stock_hybrid_horizontal_full_m = kVirtualStockProductHybridHorizontalFullDefaultM;
    float virtual_stock_hybrid_horizontal_release_m = kVirtualStockProductHybridHorizontalReleaseDefaultM;
    // Optional stateless release as the primary controller moves away from
    // the full rear target. Disabled preserves the current stock-strength
    // behavior without reading the proximity thresholds.
    bool virtual_stock_proximity_release = kVirtualStockProximityReleaseDefault;
    float virtual_stock_proximity_full_m = kVirtualStockProximityFullDefaultM;
    float virtual_stock_proximity_release_m = kVirtualStockV7ProximityReleaseDefaultM;
    // Reduce support-hand rotation by using the grip-pose position as the
    // support endpoint. Position still steers two-handed aim.
    bool two_hand_support_grip_pose = kTwoHandSupportGripPoseDefault;
    // Durable owner-bound support grip. New configurations enable it; parsing
    // an existing explicit value keeps that user's choice without migration.
    bool persistent_support_grip = true;
    // VR-shooter-style controller-input smoothing strength: the fixed speed-25
    // response with the quaternion-native MCC rotational implementation is
    // unchanged, and this value wet/dry mixes its output over raw directional
    // input copies. 0 = off/raw; 25 = the full fixed filter; intermediate
    // values blend. Consumed by the two-hand directional solver with Virtual
    // Stock on or off (the VS-ON solve takes the same directional copies; the
    // VS-OFF-only product geometry and Lab keep their own gates). Legacy
    // boolean key two_hand_smoothing migrates to 0 or 25.
    float two_hand_smoothing_strength = kTwoHandSmoothingStrengthDefault;
    // Free two-hand (VS-OFF) support-steering authority: how much the support
    // (offhand) controller's directional aim steers the presented aim line
    // while a free two-hand hold is active. 0 = the primary controller's own
    // aim is authoritative; 1 = equal authority. Consumed only by the VS-OFF
    // free two-hand solver; Virtual Stock keeps its own offhand influence.
    float two_hand_offhand_influence = kTwoHandOffhandInfluenceDefault;
    // Retired 2026-09-29: the fixed 200 ms VS-OFF acquire/release transition
    // continuity is internal product behaviour with no player control. This
    // legacy field is still parsed so historical files load quietly, but it is
    // dormant: no product or telemetry resolution may read it. The product
    // truth is TwoHandTransitionContinuityEnabled().
    bool two_hand_transition_smoothing = true;
    // Persistent-grip weapon-switch option (PG ON only). When on, a proven
    // weapon switch while the grip is engaged rebinds the two-hand hold to the
    // new weapon immediately, with no re-orientation. Off: the switched weapon
    // becomes grippable again through the normal spatial admission without
    // releasing the grip button.
    bool two_hand_switch_inherit = false;
    // Two-hand engage style: true = toggle (click left grip on/off), false =
    // hold (engaged only while the left grip is held, and only
    // where the hand reaches the gun - a real grab). Hold is the default since
    // 2026-10-08: toggle felt like "just clicking" in the headset.
    bool two_hand_toggle = false;
    // Wrist-to-palm correction for the left controller's support-hand seating
    // (IK/hand placement). Excluded from the two-hand aim line, which uses
    // raw tracked controller points. Negative seats the hand back toward the
    // wrist; -0.063 is the headset-tuned release default.
    float left_hand_forward_m = -0.063f;
    // Sideways nudge of the two-hand grab zone along the right controller's +X
    // (positive = toward the player's right) so the grab line sits on the
    // visible barrel. Headset request 2026-07-19: the AR's barrel sat right of
    // the zone and the left hand reached past it.
    float two_hand_zone_right_m = 0.03f;
    // Extra two-hand grab radius: widens the barrel grab zone sideways and
    // lengthens it at both ends. 0 = the weapon's zone exactly.
    // 2026-10-09 (v7 defaults; older configs keep 0, 0.5/0.5 and off): a
    // generous grab zone, grip hysteresis and a fresh press to grab.
    float two_hand_grab_extra_m = 0.06f;
    float two_hand_grip_press = 0.80f;   // grip counts as squeezed above this
    float two_hand_grip_release = 0.65f; // ...and let go at or below this
    bool two_hand_fresh_press = true;    // hold mode grabs only within 250 ms of the press
    // Engaged two-hand aim = the rear-hand -> support-hand line exactly (full
    // offhand influence, no mount yaw/pitch trim). v7 on; older configs off.
    bool two_hand_exact_line = true;
    // One-handed weapon classes (pistols, needler): grabbing with the support
    // hand only seats that hand on the gun; the aim stays single-handed. Long
    // guns keep the two-hand aim. v7 on; older configs off.
    bool pistol_two_hand_visual_only = true;
    // Two-hand grab zone tuned for the held weapon (F1 Weapon tuning; saved
    // per weapon with per-gun alignment). All three > 0: a zone centred
    // centre_m along the barrel, +/- half_length_m, radius_m wide. Any 0 =
    // the weapon's profile zone (weapon_profile::GrabZoneFor).
    float two_hand_grab_centre_m = 0.0f;
    float two_hand_grab_half_length_m = 0.0f;
    float two_hand_grab_radius_m = 0.0f;
    // Support-hand haptic tick on two-hand grab (release ticks at half). 0 = off.
    float two_hand_grab_haptic = 0.20f;
    // Support-controller-to-palm depth for two-hand grab acquisition only,
    // along support forward. Moves the grab sample from the tracked
    // controller toward the visible palm. Never enters aim geometry,
    // rendered-hand seating, or shots. Headset-confirmed 2026-07-19:
    // two-hand grab described as perfect with this.
    float left_grip_forward_m = 0.097f;

    // VRIK first stage: show the player's real body (game-animated) by flipping
    // the engine's director/viewmodel switches. Experimental gate for the
    // upper-body VRIK plan.
    bool body_wip = false;
    // Separate opt-in tracked avatar; unsupported rigs retain first-person hands.
    bool experimental_body_ik = false;
    bool body_ik_hide_lower = false;

    // VRIK arm IK: bend the first-person arm (shoulder planted, elbow solved,
    // hand+gun to the controller) instead of rigid-parenting the whole
    // assembly. ON = articulated arm (the accepted, working Halo 3 / ODST
    // behavior); OFF = the previous rigid parent. Required on Halo 3 / ODST, so
    // it stays ON by default. Reach's first-person weapon RENDERING is still
    // experimental (its FP layer warps), but that is a Reach render-path issue,
    // not the IK math — do not disable IK globally to work around it.
    bool arm_ik = true;

    // Floating-hands presentation (ON by default). Shows only the hands and the
    // guns they hold; the upper arms and forearms are hidden. This is a pure
    // render filter layered ON TOP of the untouched VRIK solve: the hands are
    // still tracked to the controllers exactly as before, and every arm/aim/
    // dual-wield calculation is unchanged. It only collapses the non-hand,
    // non-gun bones in the final visible palette so their geometry disappears.
    bool floating_hands = true;
    // Halo 3 / ODST: arm joints collapse onto their wrist instead of
    // shrinking in place. OFF by default until testing confirms two-hand aim and
    // melee are unaffected (H3 two-hand aim regressed when this first landed).
    bool floating_hands_collapse_at_wrist = false;

    // Opt-in physical contact between the tracked hands / held weapon and
    // engine world geometry. The setting is shared so each title can adopt
    // it without another config migration; Halo 4 and Halo 2 implement it.
    bool world_collision = false;

    // Physical contact melee and gesture input are independently optional.
    // Higher required speed makes both less sensitive; existing saves persist.
    // Immersive (v7): swing melee is on for new configs; v6 and older keep off.
    bool physical_melee = true;
    // Fist melee (v7 on, older off): a bare hand strikes only as a fist (grip
    // squeezed in open air); a held weapon strikes as before.
    bool melee_fist = true;
    // Support-hand punch (v7 on): the empty support hand strikes without a squeeze, like the gun hand.
    bool melee_support_free = true;
    // Scope lens drawn at the rendered gun node + kit glass offset (H3/ODST), not from the aim pose + a frame guess.
    bool scope_lens_node_anchor = true;
    // Lens window uses the gun's own scope shape (sniper rounded rectangle, beam rifle oval, ...) instead of a circle.
    bool scope_window_shape = true;
    // The monitor shows the game's flat view, never the XR layers: draw the
    // scope glass picture as a corner inset on the desktop window (H2/H3/ODST/
    // Reach/H4) so someone watching the monitor sees the zoom.
    bool mirror_scope_inset = true;
    // Scope parallax and eyebox: each eye sees the
    // glass picture shifted by its own angle (the picture sits
    // scope_parallax_depth glass-widths behind the glass) and a scope shadow
    // that closes as the eye leaves the lens axis (full picture within
    // scope_eyebox_tolerance_m, black after a further scope_eyebox_fade_m).
    // The eye farther from the axis keeps at least scope_eyebox_other_eye.
    bool scope_parallax = true;
    float scope_parallax_depth = 0.5f;
    bool scope_eyebox = true;
    float scope_eyebox_tolerance_m = 0.03f;
    float scope_eyebox_fade_m = 0.05f;
    float scope_eyebox_other_eye = 0.2f;
    // The glass is always live: the picture shift is capped and
    // the eyebox never darkens below scope_eyebox_floor.
    float scope_parallax_max_shift = 0.25f;
    float scope_eyebox_floor = 0.35f;
    // Diagnostic for automated test runs (saved only while set): a fixed scope pass scale for the
    // titles that render a scaled pass (top-left scale x scale of the target; H3: the
    // camera's window bounds, the split-screen path); it overrides
    // scope_adaptive_resolution. 1 = off (adaptive). 0.25-1.
    float scope_render_scale_diag = 1.0f;
    // Diagnostic for automated test runs (saved only while set): a full-size scope pass of an opted-in
    // title busy-waits this many ms on the render thread, modelling a pass too heavy for
    // the budget, so the adaptive scale's drop, held recovery and relapse (doubled hold)
    // can be forced on a rig that never misses. Half-size passes do not wait. 0 = off. 0-20.
    float scope_cost_diag_ms = 0.0f;
    bool gesture_melee = false;
    float physical_melee_swing_speed = kPhysicalMeleeSpeedDefault;
    float physical_melee_cooldown_s = kPhysicalMeleeCooldownDefaultS; // per hand, between strikes

    // Lower the RIGHT (weapon) shoulder so Master Chief's arm doesn't clip up
    // into your face — drops the shoulder anchor along your view-down axis.
    // 0 = the game's authored (high) shoulder; higher = lower shoulder. Tune
    // to match your left shoulder. In world units (~1 wu = 3 m).
    float right_shoulder_drop = 0.06f;

    // Keep the IK shoulders LEVEL with the horizon instead of pitching with your
    // head — anchor the arms to a torso frame that turns with your heading (yaw)
    // but not pitch/roll, so looking up/down no longer swings the shoulder into
    // your face. The gravity/up axis is now MEASURED from the engine's camera-up
    // (the first attempt hardcoded the wrong axis). ON by default; OFF = the old
    // behavior (shoulders ride the camera). Hand and gun are unaffected either way.
    bool shoulder_level = true;

    // Push BOTH shoulder anchors back toward your torso, along your (leveled)
    // heading. Some titles (e.g. ODST) plant the first-person shoulders in front
    // of you; raise this until they sit at your body. 0 = the game's authored
    // shoulder position (Halo 3's confirmed default); higher = further back. In
    // world units (~1 wu = 3 m). Negative pushes them forward.
    float shoulder_back_m = 0.0f;

    // Halo 3's camera motion blur. In two-render stereo its "previous frame"
    // camera is the other eye's, smearing bright content into repeated echoes
    // (the long-standing first-eye ghost). Off is also the VR comfort
    // standard. 0 = title-native blur amount suppressed (default), 1 = engine
    // values.
    bool motion_blur = false;

    // (bullet_snap retired: the composed-wrist snap spun the right hand and
    // sent bullets stage-left; reverted. The real fix is a runtime fire hook —
    // see docs and the bullet_probe diagnostic below.)

    // DIAGNOSTIC (off by default). Ignores the controller and shoves the whole
    // composed first-person assembly a fixed distance to the left. Answers one
    // binary question that the disassembly cannot: does the visible gun MESH
    // consume the bone matrices we edit?
    bool weapon_probe = false;

    // DIAGNOSTIC (off by default). Logs the CHUD state-byte window whenever it
    // changes, to locate the reticle "on target" (enemy red) state and the
    // per-element visibility flags. Log-only; changes nothing.
    bool hud_probe = false;

    // DIAGNOSTIC (off by default; set fsr_probe=1 in the cfg to investigate
    // MCC's built-in FSR). Log-only: on each distinct slot-0 render target MCC
    // binds, records its width/height/format/bind-flags and the current
    // viewport. When MCC's FSR is toggled on/off, a new line reveals whether FSR
    // makes the scene render into a SMALLER target (so the mod must capture the
    // pre-upscale image) or changes the VIEWPORT rect (so the mod's projection
    // assumptions break). Changes nothing; it only observes.
    bool fsr_probe = false;

    // DIAGNOSTIC (off by default; set bullet_probe=1 in the cfg for the
    // fire-hook hunt). On each shot, logs the camera (where Halo spawns your
    // bullet) vs the gun muzzle world position, to measure the "bullets from
    // thin air" gap. Log-only; the actual origin-move needs a runtime fire hook.
    bool bullet_probe = false;

    // Ghosting diagnostic, and the one reliable way to reproduce the open
    // left-eye ghost bug on demand: render the right eye first and the trails
    // move to the right lens.
    bool right_eye_first = false;

    // Per-title weapon/hand/HUD profiles. Every supported game -
    // and Halo 2's Anniversary and Classic renderers separately - keeps its
    // OWN copy of the thirteen tunables below, exactly like the per-vehicle
    // seat trims. The gun_*/left_hand_*/hud_* fields ABOVE are the live
    // values the engine reads; the title worker copies the active game's
    // profile into them when the game (or Halo 2 renderer) changes, and the
    // F1 sliders edit the active game's profile. `base_tunables` holds the
    // shared defaults a new profile starts from. Stored in the one
    // halomccvr.cfg as halo3_/odst_/reach_/halo4_/halo2a_/halo2c_ keys.
    TitleTunables base_tunables{};
    TitleTunables title_profiles[kTitleProfileCount]{};
    bool per_gun_alignment = true;
};

inline bool VirtualStockUsesPlusMode(const Config& config) noexcept
{
    return config.virtual_stock_rear_reference == 3;
}

inline float& VirtualStockActiveStrength(Config& config) noexcept
{
    return VirtualStockUsesPlusMode(config) ? config.virtual_stock_plus_strength
                                            : config.virtual_stock_standard_strength;
}

inline float VirtualStockActiveStrength(const Config& config) noexcept
{
    return VirtualStockUsesPlusMode(config) ? config.virtual_stock_plus_strength
                                            : config.virtual_stock_standard_strength;
}

inline bool VirtualStockUsesShoulderReference(const Config& config) noexcept
{
    return VirtualStockUsesPlusMode(config) ? config.virtual_stock_hybrid_ads_reference == 1
                                            : config.virtual_stock_rear_reference == 1;
}

// Diagnostic-only capture family. Head-turn sway correction was retired
// from the product on 2026-09-27, so product resolution can never activate it
// (VirtualStockAimSettingsFromConfig always resolves inverse-neck off). It
// survives for isolated diagnostic profiles and their neutral-capture
// experiments. When it is somehow active it is preserved across
// mode/reference/tuning changes; presentation recenter does not invalidate;
// real epoch transitions do via the existing state machine.
inline bool VirtualStockHeadTurnCorrectionFamilyActive(const VirtualStockAimSettings& settings) noexcept
{
    return settings.virtualStockEnabled && settings.hybridInverseNeckEnabled;
}

inline void SetVirtualStockPlusMode(Config& config, bool plus) noexcept
{
    if (plus)
    {
        if (!VirtualStockUsesPlusMode(config))
        {
            config.virtual_stock_hybrid_ads_reference = config.virtual_stock_rear_reference == 1 ? 1 : 0;
            config.virtual_stock_rear_reference = 3;
        }
    }
    else if (VirtualStockUsesPlusMode(config))
    {
        config.virtual_stock_rear_reference = config.virtual_stock_hybrid_ads_reference == 1 ? 1 : 0;
    }
}

inline void SetVirtualStockShoulderReference(Config& config, bool shoulder) noexcept
{
    const int reference = shoulder ? 1 : 0;
    if (VirtualStockUsesPlusMode(config))
        config.virtual_stock_hybrid_ads_reference = reference;
    else
        config.virtual_stock_rear_reference = reference;
}

inline void ResetVirtualStockSettings(Config& config) noexcept
{
    const bool enabled = config.virtual_stock;
    config.virtual_stock = kVirtualStockEnabledDefault;
    config.virtual_stock_standard_strength = kVirtualStockV7StrengthDefault;
    config.virtual_stock_plus_strength = kVirtualStockV7StrengthDefault;
    config.virtual_stock_rear_height_m = kVirtualStockRearHeightDefaultM;
    config.virtual_stock_rear_reference = kVirtualStockProductRearReferenceDefault;
    config.virtual_stock_shoulder_back_m = kVirtualStockShoulderBackDefaultM;
    config.virtual_stock_shoulder_side_m = kVirtualStockShoulderSideDefaultM;
    config.virtual_stock_chest_height_m = kVirtualStockChestHeightDefaultM;
    config.virtual_stock_chest_back_m = kVirtualStockChestBackDefaultM;
    config.virtual_stock_chest_side_m = kVirtualStockChestSideDefaultM;
    config.virtual_stock_adaptive_top_height_m = kVirtualStockAdaptiveTopHeightDefaultM;
    config.virtual_stock_adaptive_bottom_height_m = kVirtualStockAdaptiveBottomHeightDefaultM;
    config.virtual_stock_adaptive_top_half_width_m = kVirtualStockAdaptiveTopHalfWidthDefaultM;
    config.virtual_stock_adaptive_bottom_half_width_m = kVirtualStockAdaptiveBottomHalfWidthDefaultM;
    config.virtual_stock_hybrid_offhand_influence = kVirtualStockHybridOffhandInfluenceDefault;
    config.virtual_stock_hybrid_ads_reference = kVirtualStockHybridAdsReferenceDefault;
    config.virtual_stock_hybrid_seat_full_m = kVirtualStockProductHybridSeatFullDefaultM;
    config.virtual_stock_hybrid_seat_release_m = kVirtualStockProductHybridSeatReleaseDefaultM;
    config.virtual_stock_hybrid_horizontal_release = kVirtualStockProductHybridHorizontalReleaseDefault;
    config.virtual_stock_hybrid_horizontal_full_m = kVirtualStockProductHybridHorizontalFullDefaultM;
    config.virtual_stock_hybrid_horizontal_release_m = kVirtualStockProductHybridHorizontalReleaseDefaultM;
    config.virtual_stock_proximity_release = kVirtualStockProximityReleaseDefault;
    config.virtual_stock_proximity_full_m = kVirtualStockProximityFullDefaultM;
    config.virtual_stock_proximity_release_m = kVirtualStockV7ProximityReleaseDefaultM;
    config.virtual_stock = enabled;
}

inline VirtualStockAimSettings VirtualStockAimSettingsFromConfig(
    const Config& config, HybridDiagnosticOverride diagnosticOverride) noexcept
{
    VirtualStockAimSettings settings{};
    settings.virtualStockEnabled = config.virtual_stock;
    settings.virtualStockStrength = VirtualStockActiveStrength(config);
    settings.virtualStockRearHeightM = config.virtual_stock_rear_height_m;
    settings.virtualStockRearReference = config.virtual_stock_rear_reference;
    settings.virtualStockShoulderBackM = config.virtual_stock_shoulder_back_m;
    settings.virtualStockShoulderSideM = config.virtual_stock_shoulder_side_m;
    settings.virtualStockChestHeightM = config.virtual_stock_chest_height_m;
    settings.virtualStockChestBackM = config.virtual_stock_chest_back_m;
    settings.virtualStockChestSideM = config.virtual_stock_chest_side_m;
    settings.virtualStockAdaptiveTopHeightM = config.virtual_stock_adaptive_top_height_m;
    settings.virtualStockAdaptiveBottomHeightM = config.virtual_stock_adaptive_bottom_height_m;
    settings.virtualStockAdaptiveTopHalfWidthM = config.virtual_stock_adaptive_top_half_width_m;
    settings.virtualStockAdaptiveBottomHalfWidthM = config.virtual_stock_adaptive_bottom_half_width_m;
    settings.virtualStockHybridOffhandInfluence = config.virtual_stock_hybrid_offhand_influence;
    settings.virtualStockHybridAdsReference = config.virtual_stock_hybrid_ads_reference;
    settings.virtualStockHybridSeatFullM = config.virtual_stock_hybrid_seat_full_m;
    settings.virtualStockHybridSeatReleaseM = config.virtual_stock_hybrid_seat_release_m;
    settings.hybridHorizontalRearReleaseEnabled = config.virtual_stock_hybrid_horizontal_release;
    settings.hybridHorizontalRearReleaseFullM = config.virtual_stock_hybrid_horizontal_full_m;
    settings.hybridHorizontalRearReleaseReleaseM = config.virtual_stock_hybrid_horizontal_release_m;
    settings.hybridDiagnosticOverride = NormalizeHybridDiagnosticOverride(static_cast<uint8_t>(diagnosticOverride));
    // Planted Standard: normal product Centre/Shoulder ignores ordinary legacy
    // proximity attenuation. The persisted keys stay compatible/dormant; the
    // effective resolver forces disabled for rear 0/1 only. Plus (3) already
    // ignores ordinary proximity via its seat/horizontal path; Chest (2) keeps
    // persisted behaviour as dormant research.
    const bool standardProduct = settings.virtualStockRearReference == 0 || settings.virtualStockRearReference == 1;
    settings.virtualStockProximityRelease = standardProduct ? false : config.virtual_stock_proximity_release;
    settings.virtualStockProximityFullM = config.virtual_stock_proximity_full_m;
    settings.virtualStockProximityReleaseM = config.virtual_stock_proximity_release_m;
    // Head-turn sway correction was retired from the product (2026-09-27).
    // Normal Standard/Plus resolution is the former sway=0 path: inverse-neck
    // disabled with zeroed knobs. The generic NK capability that remains is
    // reachable only through isolated diagnostic profiles; product resolution
    // can never enable it here.
    settings.hybridInverseNeckEnabled = false;
    settings.hybridInverseNeckStrength = kVirtualStockHybridInverseNeckStrengthDefault;
    settings.hybridInverseNeckForwardM = kVirtualStockHybridInverseNeckForwardDefaultM;
    settings.hybridInverseNeckUpM = kVirtualStockHybridInverseNeckUpDefaultM;
    settings.hybridInverseNeckLateralM = kVirtualStockHybridInverseNeckLateralDefaultM;
    return settings;
}

extern Config g_config;

// Per-title profile control. `profile` is 0..5 (Halo 3, ODST, Reach,
// Halo 4, Halo 2 Anniversary, Halo 2 Classic) or -1 for the shared
// defaults. Apply copies that profile into the live tunables (no-op when
// already active); Store copies the live tunables back into the active
// profile (the F1 save path). Both are cheap and safe on any thread.
const char* Config_TitleProfileName(int profile);
int Config_ActiveTitleProfile();
void Config_ApplyTitleProfile(int profile);
// Copies the shared value of one per-game key (its cfg
// suffix, e.g. "aa_mode") into the running game's profile and the live field.
// Returns false for an unknown key or when no game is active.
bool Config_UseSharedValue(const char* suffix);
void Config_StoreLiveTunables();
// Cold config/UI path only. Identity comes from the admitted primary weapon
// palette, never a native object/tag handle. Unknown models use title settings.
void Config_ApplyWeaponProfile(uint64_t identity);
// Config_ApplyWeaponProfile for the render thread, reporting how
// long it waited for the profile lock (0 when the lock was free).
void Config_ApplyWeaponProfileTimed(uint64_t identity, uint64_t& waitedMicroseconds);
void Config_RefreshWeaponProfile();
// The weapon identity whose placement/lens row the live scope_screen_* fields hold
// (published when Config_ApplyWeaponProfile returns; lock-free for the render thread).
uint64_t Config_AppliedWeaponIdentity();
const char* Config_ActiveWeaponProfileName();
// Copy of the loaded config file's path (empty before ConfigLoad).
std::wstring Config_GetPath();

// The trim a given SEAT actually uses: its own override when one has been
// set, the universal trim otherwise. `slot` comes from ConfigSeatTrimSlot;
// -1 (on foot, unknown vehicle, unauthored seat) reads the universal trim.
// Enclosed-cockpit default: a seat that
// has no trim of its own lifts the eye toward the open hatch so the driver can
// see out. Metres up; HEADSET-UNVERIFIED starting points (the Halo 3 / ODST
// Scorpion already ships 1.50 from the maintainer's tuning), meant to be
// adjusted with the per-seat F1 sliders. Only when vehicle_cockpit_eye_up is on
// (v7); an explicit per-seat trim always wins.
inline constexpr float kCockpitEyeUpWraithM = 2.4f; // Halo 3 / ODST wraith driver
inline constexpr float kCockpitEyeUpH2ScorpionM = 1.5f;
inline constexpr float kCockpitEyeUpH2WraithM = 2.0f;
inline constexpr uint64_t kHalo2ScorpionIdentity = 0x1E6E302886CF3D7Bull;
inline constexpr uint64_t kHalo2WraithIdentity = 0x55D8FE797FA258C7ull;
inline float ConfigCockpitEyeUpFallback(const Config& c, GameTitle title, int slot)
{
    if (!c.vehicle_cockpit_eye_up)
        return 0.0f;
    if (title == GameTitle::Halo3 && slot >= 0 && slot == ConfigSeatTrimSlot(5, 0, false))
        return kCockpitEyeUpWraithM;
    if (title == GameTitle::Halo3ODST && slot >= 0 && slot == ConfigOdstSeatTrimSlot(5, 0, false))
        return kCockpitEyeUpWraithM;
    return 0.0f;
}
// Quad size of an independent dual-wield reticle: the crosshair size times the
// dual scale (finite, clamped so a bad value never hides or fills the view).
inline float ConfigDualReticleSize(const Config& c, float crosshairQuadSize)
{
    const float s = std::isfinite(c.dual_reticle_scale) ? std::clamp(c.dual_reticle_scale, 0.1f, 1.5f) : 0.25f;
    return std::isfinite(crosshairQuadSize) ? crosshairQuadSize * s : 0.0f;
}
inline float ConfigGameVehicleCam(const Config& c, GameTitle title, unsigned axis)
{
    const int index = static_cast<int>(title) - 1;
    if (axis > 2)
        return 0.f;
    if (index >= 0 && index < 6 && c.vehicle_cam_game_set[index][axis])
        return c.vehicle_cam_game[index][axis];
    return axis == 0 ? c.vehicle_cam_forward_m : axis == 1 ? c.vehicle_cam_up_m : c.vehicle_cam_right_m;
}
inline int ConfigVehicleModelTrimSlot(const Config& c, GameTitle title, uint64_t identity, int seat)
{
    if (!identity || seat < 0 || seat > 31 || title < GameTitle::Halo3 || title > GameTitle::Halo2)
        return -1;
    for (unsigned i = 0; i < kVehicleModelTrimCapacity; ++i)
        if (c.vehicle_model_trims[i].identity == identity && c.vehicle_model_trims[i].title == title &&
            c.vehicle_model_trims[i].seat == seat)
            return static_cast<int>(i);
    return -1;
}
inline int ConfigEnsureVehicleModelTrim(Config& c, GameTitle title, uint64_t identity, int seat)
{
    const int existing = ConfigVehicleModelTrimSlot(c, title, identity, seat);
    if (existing >= 0)
        return existing;
    if (!identity || seat < 0 || seat > 31 || title < GameTitle::Halo3 || title > GameTitle::Halo2)
        return -1;
    for (unsigned i = 0; i < kVehicleModelTrimCapacity; ++i)
        if (!c.vehicle_model_trims[i].identity)
        {
            c.vehicle_model_trims[i] = {title, identity, static_cast<int16_t>(seat)};
            return static_cast<int>(i);
        }
    return -1;
}
inline float ConfigVehicleModelCam(const Config& c, GameTitle title, uint64_t identity, int seat, unsigned axis)
{
    const int slot = ConfigVehicleModelTrimSlot(c, title, identity, seat);
    if (axis < 3 && slot >= 0 && c.vehicle_model_trims[slot].set[axis])
        return c.vehicle_model_trims[slot].value[axis];
    if (axis == 1 && c.vehicle_cockpit_eye_up && title == GameTitle::Halo2 && seat == 0)
    {
        if (identity == kHalo2ScorpionIdentity)
            return kCockpitEyeUpH2ScorpionM;
        if (identity == kHalo2WraithIdentity)
            return kCockpitEyeUpH2WraithM;
    }
    return ConfigGameVehicleCam(c, title, axis);
}
inline float ConfigSeatCamForward(const Config& c, int slot)
{
    if (slot >= 0 && slot < kVehicleTrimSlots && c.vehicle_cam_forward_set[slot])
        return c.vehicle_cam_forward_v[slot];
    return ConfigGameVehicleCam(c, GameTitle::Halo3, 0);
}

inline float ConfigSeatCamUp(const Config& c, int slot)
{
    if (slot >= 0 && slot < kVehicleTrimSlots && c.vehicle_cam_up_set[slot])
        return c.vehicle_cam_up_v[slot];
    if (const float eyeUp = ConfigCockpitEyeUpFallback(c, GameTitle::Halo3, slot))
        return eyeUp;
    return ConfigGameVehicleCam(c, GameTitle::Halo3, 1);
}

inline float ConfigSeatCamRight(const Config& c, int slot)
{
    if (slot >= 0 && slot < kVehicleTrimSlots && c.vehicle_cam_right_set[slot])
        return c.vehicle_cam_right_v[slot];
    return ConfigGameVehicleCam(c, GameTitle::Halo3, 2);
}

// ODST's bank. `slot` comes from ConfigOdstSeatTrimSlot; an unset ODST seat
// falls back to the same universal trim Halo 3 uses, so the universal slider
// still means "every seat in every title".
inline float ConfigOdstSeatCamForward(const Config& c, int slot)
{
    if (slot >= 0 && slot < kOdstVehicleTrimSlots && c.odst_vehicle_cam_forward_set[slot])
        return c.odst_vehicle_cam_forward_v[slot];
    return ConfigGameVehicleCam(c, GameTitle::Halo3ODST, 0);
}

inline float ConfigOdstSeatCamUp(const Config& c, int slot)
{
    if (slot >= 0 && slot < kOdstVehicleTrimSlots && c.odst_vehicle_cam_up_set[slot])
        return c.odst_vehicle_cam_up_v[slot];
    if (const float eyeUp = ConfigCockpitEyeUpFallback(c, GameTitle::Halo3ODST, slot))
        return eyeUp;
    return ConfigGameVehicleCam(c, GameTitle::Halo3ODST, 1);
}

inline float ConfigOdstSeatCamRight(const Config& c, int slot)
{
    if (slot >= 0 && slot < kOdstVehicleTrimSlots && c.odst_vehicle_cam_right_set[slot])
        return c.odst_vehicle_cam_right_v[slot];
    return ConfigGameVehicleCam(c, GameTitle::Halo3ODST, 2);
}

// The Blender lineup is the BASE of a Reach seat, so a slot that has an
// authored row must never fall through to the shared universal trim. A
// 2026-08-06 play session proved why: a polluted universal (-0.765 / +0.894) reached
// the Warthog driver through its `use_universal` tombstone and moved the seat
// almost a metre. `outHasBase` reports whether the row exists so the F1 slider
// can hang Halo 3's own travel off it.
inline constexpr bool ConfigReachSeatAuthoredBase(int slot, float* outForward, float* outUp, float* outRight)
{
    if (outForward)
        *outForward = 0.0f;
    if (outUp)
        *outUp = 0.0f;
    if (outRight)
        *outRight = 0.0f;
    if (slot < 0 || slot >= kReachVehicleTrimSlots)
        return false;
    for (const ConfigReachShippedSeatTrim& trim : kConfigReachShippedSeatTrims)
    {
        if (ConfigReachSeatTrimSlot(trim.vehicleId, trim.seatIndex) != slot)
            continue;
        if (outForward)
            *outForward = trim.forward;
        if (outUp)
            *outUp = trim.up;
        if (outRight)
            *outRight = trim.right;
        return true;
    }
    return false;
}

// Reach's bank follows the same universal fallback contract without sharing
// storage with Halo 3 or ODST. A serialized seat tombstone returns the seat to
// its authored Blender base; only a seat that never had one follows the
// universal trim.
inline float ConfigReachSeatCamForward(const Config& c, int slot)
{
    if (slot >= 0 && slot < kReachVehicleTrimSlots)
    {
        if (!c.reach_vehicle_cam_use_universal[slot] && c.reach_vehicle_cam_forward_set[slot])
            return c.reach_vehicle_cam_forward_v[slot];
        float authored = 0.0f;
        if (ConfigReachSeatAuthoredBase(slot, &authored, nullptr, nullptr))
            return authored;
    }
    return ConfigGameVehicleCam(c, GameTitle::HaloReach, 0);
}

inline float ConfigReachSeatCamUp(const Config& c, int slot)
{
    if (slot >= 0 && slot < kReachVehicleTrimSlots)
    {
        if (!c.reach_vehicle_cam_use_universal[slot] && c.reach_vehicle_cam_up_set[slot])
            return c.reach_vehicle_cam_up_v[slot];
        float authored = 0.0f;
        if (ConfigReachSeatAuthoredBase(slot, nullptr, &authored, nullptr))
            return authored;
    }
    return ConfigGameVehicleCam(c, GameTitle::HaloReach, 1);
}

inline float ConfigReachSeatCamRight(const Config& c, int slot)
{
    if (slot >= 0 && slot < kReachVehicleTrimSlots)
    {
        if (!c.reach_vehicle_cam_use_universal[slot] && c.reach_vehicle_cam_right_set[slot])
            return c.reach_vehicle_cam_right_v[slot];
        float authored = 0.0f;
        if (ConfigReachSeatAuthoredBase(slot, nullptr, nullptr, &authored))
            return authored;
    }
    return ConfigGameVehicleCam(c, GameTitle::HaloReach, 2);
}

inline void ConfigReachSeatUseUniversalTrim(Config& c, int slot)
{
    if (slot < 0 || slot >= kReachVehicleTrimSlots)
        return;
    c.reach_vehicle_cam_forward_set[slot] = false;
    c.reach_vehicle_cam_up_set[slot] = false;
    c.reach_vehicle_cam_right_set[slot] = false;
    c.reach_vehicle_cam_use_universal[slot] = true;
}

// The first F1 move after an explicit reset converts the whole displayed
// triplet into a seat override before changing one axis. It must capture
// exactly what the sliders are showing, which is the authored Blender row when
// the seat has one and the universal trim only when it does not.
inline void ConfigReachSeatBeginTrimEdit(Config& c, int slot)
{
    if (slot < 0 || slot >= kReachVehicleTrimSlots || !c.reach_vehicle_cam_use_universal[slot])
        return;
    c.reach_vehicle_cam_forward_v[slot] = ConfigReachSeatCamForward(c, slot);
    c.reach_vehicle_cam_up_v[slot] = ConfigReachSeatCamUp(c, slot);
    c.reach_vehicle_cam_right_v[slot] = ConfigReachSeatCamRight(c, slot);
    c.reach_vehicle_cam_forward_set[slot] = true;
    c.reach_vehicle_cam_up_set[slot] = true;
    c.reach_vehicle_cam_right_set[slot] = true;
    c.reach_vehicle_cam_use_universal[slot] = false;
}

void ConfigLoad(const wchar_t* path); // missing file -> file is created with defaults
void ConfigLoadMigrating(const wchar_t* primaryPath, const wchar_t* legacyPath);
void ConfigSave();
// Copy the settings file to <file>.bak before "Reset ALL settings".
bool Config_BackupForReset();
