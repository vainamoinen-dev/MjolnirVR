// core_aim_scope_hud_tests.cpp: scope cameras and zoom, reticle capture and settle, authored layers, menu sliders and virtual-stock defaults.
// Units 0, 1, 24, 25, 26, 116, 117, 121, 225, 232, 234, 240, 245, 247, 248 of the former tests/core_tests.cpp main(), moved unchanged and in
// their order (E3). Built as halomccvr_core_aim_scope_hud_tests and run by ctest; each unit prints its check count.
#include "core_test_support.h"

int main()
{
    CheckUnit(0);
    {
        const float barrel[3]{0, 2, 0}, aim[3]{3, 0, 0}, zero[3]{};
        float out[3]{};
        const bool usesBarrel = SelectScopeForward(barrel, true, aim, out) && out[1] == 1.0f;
        const bool staleFallsBack = !SelectScopeForward(barrel, false, aim, out) && out[0] == 1.0f;
        const bool badFallsBack = !SelectScopeForward(zero, true, aim, out) && out[0] == 1.0f;
        float yaw = 0, pitch = 0, total = 0;
        const float a[3]{1, 0, 0}, b[3]{0.99939f, 0.0348995f, 0}; // 2 deg left
        DirectionDeltaDegrees(a, b, yaw, pitch, total);
        const bool split = std::fabs(yaw - 2.0f) < 0.01f && std::fabs(pitch) < 0.01f && std::fabs(total - 2.0f) < 0.01f;
        Check(usesBarrel && staleFallsBack && badFallsBack && split,
            "H3 scope camera: barrel when fresh, engine aim otherwise; yaw/pitch split (#4)");
    }
    CheckUnit(1);
    {
        int texture = 0, other = 0;
        struct View
        {
            const void* resource;
        };
        View slotView{&texture}, engineView{&texture}, foreign{&other};
        View* bound[]{&foreign, nullptr, &engineView};
        const auto resourceOf = [](View* v) -> const void* { return v->resource; };
        Check(AnyBoundViewOf(&texture, 3, bound, resourceOf) && bound[2] != &slotView &&
                  !AnyBoundViewOf(&texture, 2, bound, resourceOf) && !AnyBoundViewOf(nullptr, 3, bound, resourceOf) &&
                  !AnyBoundViewOf(&texture, 3, static_cast<View* const*>(nullptr), resourceOf),
            "H2A scope source: another view of the same texture counts as bound (#7)");
    }
    CheckUnit(24);
    {
        const float neutral[4]{1.0f, 0.3f, 0.6f, 0.9f};
        const float neutralFaded[4]{0.5f, 0.15f, 0.3f, 0.45f};
        const float friendly[4]{1.0f, 0.8f, 0.2f, 0.4f};
        const float enemy[4]{1.0f, 0.2f, 0.8f, 0.4f};
        const uint32_t neutralState = ClassifyAuthoredReticleColorOrdering(neutral);
        Check(neutralState != 0 && neutralState == ClassifyAuthoredReticleColorOrdering(neutralFaded) &&
                  neutralState != ClassifyAuthoredReticleColorOrdering(friendly) &&
                  neutralState != ClassifyAuthoredReticleColorOrdering(enemy) &&
                  ClassifyAuthoredReticleColorOrdering(friendly) != ClassifyAuthoredReticleColorOrdering(enemy),
            "authored colour ordering and fade stability");
        const float invalid[4]{1.0f, 0.0f, INFINITY, 0.0f};
        Check(ClassifyAuthoredReticleColorOrdering(invalid) == 0, "authored invalid colour rejected");
    }
    CheckUnit(25);
    {
        AuthoredReticleRefreshState state{};
        Check(!ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState, true, true, 7, 2, 0, 100, 1, state),
            "reticle settle starts");
        Check(!ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState, true, true, 7, 2, 0, 123, 1, state),
            "reticle still settling");
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState, true, true, 7, 2, 0, 124, 1, state),
            "reticle settled publish");
        MarkAuthoredReticleUploaded(state, 7, 2, 0, 124);
        Check(!ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState, true, true, 7, 2, 0, 130, 1, state),
            "same colour skipped");
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState, true, true, 7, 4, 0, 130, 1, state),
            "colour edge publish");
        MarkAuthoredReticleUploaded(state, 7, 4, 0, 130);
        Check(!ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState, true, true, 7, 8, 0, 133, 1, state),
            "colour edge throttled");
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState, true, true, 7, 8, 0, 136, 1, state),
            "colour edge after gap");
        Check(!ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState, true, true, 8, 8, 0, 200, 1, state),
            "new identity settles");
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState, true, true, 8, 8, 0, 224, 1, state),
            "new identity publishes");
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::BoundedAnimation, true, true, 7, 0, 0, 230, 1, state),
            "bounded title cadence");
        // Reported bug: Reach's crosshair vanished for a moment when the player
        // took damage. A preserved log shows the published art key
        // flipping to one weapon-independent value and back inside ~50 ms,
        // over and over during combat, while the quad stayed SUBMITTED with
        // held art - so a momentary alternate class-2 widget set was being
        // published straight over good art. A different identity must now
        // settle first; the identity already on the quad still republishes on
        // the bounded cadence, which is what keeps the animation live.
        AuthoredReticleRefreshState flickerState{};
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::BoundedAnimation, true, true, 0xABC, 0, 0, 500, 1, flickerState),
            "bounded first art publishes immediately");
        MarkAuthoredReticleUploaded(flickerState, 0xABC, 0, 0, 500);
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::BoundedAnimation, true, true, 0xABC, 0, 0, 506, 1, flickerState),
            "bounded animation republishes the same identity");
        MarkAuthoredReticleUploaded(flickerState, 0xABC, 0, 0, 506);
        // The ~50 ms transient: six frames of a different identity, then back.
        for (uint64_t frame = 512; frame < 518; ++frame)
        {
            Check(!ShouldUploadAuthoredReticle(
                      AuthoredReticleRefreshPolicy::BoundedAnimation, true, true, 0xDEF, 0, 0, frame, 1, flickerState),
                "bounded transient identity never publishes");
        }
        Check(flickerState.lastPublishedKey == 0xABC, "bounded transient leaves the held art alone");
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::BoundedAnimation, true, true, 0xABC, 0, 0, 518, 1, flickerState),
            "bounded animation resumes after the transient");
        MarkAuthoredReticleUploaded(flickerState, 0xABC, 0, 0, 518);
        // A real weapon swap still gets through: the same identity held for
        // the settle window publishes.
        Check(!ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::BoundedAnimation, true, true, 0xF00, 0, 0, 600, 1, flickerState),
            "bounded new identity settles");
        Check(!ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::BoundedAnimation, true, true, 0xF00, 0, 0, 623, 1, flickerState),
            "bounded new identity still settling");
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::BoundedAnimation, true, true, 0xF00, 0, 0, 624, 1, flickerState),
            "bounded new identity publishes once settled");
        // The harder half of the same bug: Reach's crosshair THINS OUT before it
        // stops. A second preserved log shows a two-second window uploading
        // 19 where the steady rate was 30, so frames capture a fragment of the
        // reticle under the SAME identity key. Publishing that fragment is
        // what the player sees as the crosshair disappearing when hit.
        AuthoredReticleRefreshState thinState{};
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::BoundedAnimation, true, true, 0xABC, 0, 5, 700, 1, thinState),
            "bounded full capture publishes");
        MarkAuthoredReticleUploaded(thinState, 0xABC, 0, 5, 700);
        Check(thinState.lastPublishedDraws == 5, "published piece count kept");
        for (uint64_t frame = 706; frame < 724; ++frame)
        {
            Check(!ShouldUploadAuthoredReticle(
                      AuthoredReticleRefreshPolicy::BoundedAnimation, true, true, 0xABC, 0, 1, frame, 1, thinState),
                "thinned capture never replaces good art");
        }
        Check(thinState.lastPublishedDraws == 5, "thinned capture leaves the held art alone");
        // The full crosshair coming back publishes at once - no settle debt.
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::BoundedAnimation, true, true, 0xABC, 0, 5, 724, 1, thinState),
            "full capture returns immediately");
        MarkAuthoredReticleUploaded(thinState, 0xABC, 0, 5, 724);
        // A permanently simpler crosshair is not held hostage: once the lower
        // piece count persists past the settle window it publishes.
        Check(!ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::BoundedAnimation, true, true, 0xABC, 0, 2, 800, 1, thinState),
            "sustained thinner capture starts settling");
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::BoundedAnimation, true, true, 0xABC, 0, 2, 824, 1, thinState),
            "sustained thinner capture eventually publishes");
        // A title that supplies no piece count (Halo 3, ODST) is unaffected.
        AuthoredReticleRefreshState noCountState{};
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::BoundedAnimation, true, true, 0xABC, 0, 0, 900, 1, noCountState),
            "no piece count publishes");
        MarkAuthoredReticleUploaded(noCountState, 0xABC, 0, 0, 900);
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::BoundedAnimation, true, true, 0xABC, 0, 0, 906, 1, noCountState),
            "no piece count leaves the guard inert");
        AuthoredReticleRefreshState immediateState{};
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityImmediate, true, true, 31, 0, 0, 240, 1, immediateState),
            __func__);
        MarkAuthoredReticleUploaded(immediateState, 31, 0, 0, 240);
        Check(!ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityImmediate, true, true, 31, 0, 0, 270, 1, immediateState),
            __func__);
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityImmediate, true, true, 32, 0, 0, 270, 1, immediateState),
            __func__);
        AuthoredReticleRefreshState odstState{};
        Check(!ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState, true, true, 21, 0, 0, 300, 1, odstState),
            __func__);
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState, true, true, 21, 0, 0, 324, 1, odstState),
            __func__);
        MarkAuthoredReticleUploaded(odstState, 21, 0, 0, 324);
        Check(!ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState, true, true, 21, 0, 0, 400, 1, odstState),
            __func__);
        Check(!ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState, true, true, 22, 0, 0, 400, 1, odstState),
            __func__);
        Check(ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState, true, true, 22, 0, 0, 424, 1, odstState),
            __func__);
        Check(!AuthoredReticleLayerHasContent(true, false) && AuthoredReticleLayerHasContent(true, true) &&
                  AuthoredReticleLayerHasContent(false, false),
            "held authored layer gate");
        Check(AuthoredReticleCaptureOwnsSceneBind(true, true, true) &&
                  !AuthoredReticleCaptureOwnsSceneBind(false, true, true) &&
                  !AuthoredReticleCaptureOwnsSceneBind(true, false, true) &&
                  !AuthoredReticleCaptureOwnsSceneBind(true, true, false),
            "authored capture owns only the exact active scene bind");
        Check(!ShouldUploadAuthoredReticle(
                  AuthoredReticleRefreshPolicy::IdentityAndColorState, true, true, 9, 2, 0, 400, 2, state) &&
                  state.ownerEpoch == 2 && state.lastPublishedKey == 0,
            "reticle owner reset");
    }
    CheckUnit(26);
    {
        // Offscreen capture cadence. The first sample and every widget within
        // an already-sampled frame are always admitted, so a compound reticle
        // is never captured in pieces from different frames.
        Check(ShouldSampleAuthoredCapture(6, 0, 100), "first capture sampled");
        Check(ShouldSampleAuthoredCapture(6, 100, 100), "same frame re-admitted");
        Check(!ShouldSampleAuthoredCapture(6, 100, 101), "capture held inside gap");
        Check(!ShouldSampleAuthoredCapture(6, 100, 105), "capture held to the gap");
        Check(ShouldSampleAuthoredCapture(6, 100, 106), "capture sampled at the gap");
        Check(ShouldSampleAuthoredCapture(30, 100, 130), "slow cadence sampled");
        Check(!ShouldSampleAuthoredCapture(30, 100, 129), "slow cadence held");
        Check(ShouldSampleAuthoredCapture(6, 500, 10), "serial restart re-samples");
        Check(ShouldSampleAuthoredCapture(0, 100, 101), "gap 0 never throttles");

        // The capture gap and the publish floor are deliberately the same
        // number: a sampled frame must always be allowed to publish, or the
        // capture work is spent and thrown away.
        Check(ResolveAuthoredAnimationGapFrames(6) == kMinimumUploadGapFrames &&
                  ResolveAuthoredAnimationGapFrames(1) == kMinimumUploadGapFrames &&
                  ResolveAuthoredAnimationGapFrames(12) == 12 && ResolveAuthoredAnimationGapFrames(600) == 60 &&
                  ResolveAuthoredAnimationGapFrames(0) == 0 && ResolveAuthoredAnimationGapFrames(-4) == 0,
            "authored animation cadence clamp");

        // A Halo 3 sample publishes on the frame it was captured on, every
        // time, so the animation cadence the player sets is the cadence they
        // get. This is the property the frozen-snapshot bug violated.
        AuthoredReticleRefreshState animated{};
        uint64_t lastSample = 0;
        int published = 0;
        const uint64_t gap = ResolveAuthoredAnimationGapFrames(6);
        for (uint64_t serial = 1; serial <= 120; ++serial)
        {
            if (!ShouldSampleAuthoredCapture(gap, lastSample, serial))
                continue;
            lastSample = serial;
            // The identity key is deliberately constant: an animating Halo 3
            // crosshair does not change which widgets drew.
            if (ShouldUploadAuthoredReticle(
                    AuthoredReticleRefreshPolicy::BoundedAnimation, true, true, 0xABCD, 0, 0, serial, 1, animated))
            {
                MarkAuthoredReticleUploaded(animated, 0xABCD, 0, 0, serial);
                ++published;
            }
        }
        Check(published == 20, "every animated sample publishes");

        // Halo 4 has no proven stable weapon-art identity, so animation=0
        // holds the fixed key between slow probes rather than freezing it for
        // the rest of the level. Every 30-frame sample must still publish.
        AuthoredReticleRefreshState halo4Held{};
        lastSample = 0;
        published = 0;
        for (uint64_t serial = 1; serial <= 120; ++serial)
        {
            if (!ShouldSampleAuthoredCapture(30, lastSample, serial))
                continue;
            lastSample = serial;
            if (ShouldUploadAuthoredReticle(
                    AuthoredReticleRefreshPolicy::BoundedAnimation, true, true, 1, 0, 0, serial, 4, halo4Held))
            {
                MarkAuthoredReticleUploaded(halo4Held, 1, 0, 0, serial);
                ++published;
            }
        }
        Check(published == 4, "Halo 4 held art republishes on each slow fixed-key probe");
    }
    CheckUnit(116);
    {
        // Dual-wield reticles: a quarter of the crosshair by default, clamped, never NaN.
        Config c{};
        Check(std::fabs(ConfigDualReticleSize(c, 1.0f) - 0.25f) < 1e-6f,
            "dual reticles default to a quarter of the crosshair size");
        c.dual_reticle_scale = 9.0f;
        Check(ConfigDualReticleSize(c, 1.0f) == 1.5f, "scale clamps high");
        c.dual_reticle_scale = 0.0f;
        Check(ConfigDualReticleSize(c, 1.0f) == 0.1f, "and low");
        c.dual_reticle_scale = std::numeric_limits<float>::quiet_NaN();
        Check(std::fabs(ConfigDualReticleSize(c, 1.0f) - 0.25f) < 1e-6f, "a bad scale falls back to the default");
        Check(ConfigDualReticleSize(Config{}, std::numeric_limits<float>::quiet_NaN()) == 0.0f,
            "a bad base size draws nothing");
    }
    CheckUnit(117);
    {
        // Weapon lens rows: the table lookup is exact on title + model; empty/sentinel tables match nothing.
        const weapon_profile::ScopeLensRow rows[]{{GameTitle::Halo3, "fp_sniper_rifle", 0.0f, 0.1f, 0.2f, 0.05f}};
        // Class starting placements: every Optic weapon gets one; non-optic weapons none; a captured row wins.
        {
            using namespace weapon_profile;
            const weapon_model::Model sniper{GameTitle::Halo3, 1, "fp_sniper_rifle", false, 4, 0, 0, {}, {}};
            const weapon_model::Model br{GameTitle::Halo3, 2, "fp_battle_rifle", false, 4, 0, 0, {}, {}};
            const weapon_model::Model rocket{GameTitle::Halo3, 3, "fp_rocket_launcher", false, 4, 0, 0, {}, {}};
            const weapon_model::Model ar{GameTitle::Halo3, 4, "fp_assault_rifle", false, 4, 0, 0, {}, {}};
            const weapon_model::Model unknown{GameTitle::Halo3, 5, "fp_never_heard_of_it", false, 4, 0, 0, {}, {}};
            ScopeLensRow r{};
            bool cls = false;
            Check(ClassRow(GameTitle::Halo3, "fp_sniper_rifle").width == 0.100f &&
                      ClassRow(GameTitle::Halo3, "fp_sniper_rifle").up == 0.150f,
                "sniper-class lens placement");
            Check(ScopeLensPlacement(&sniper, r, cls) && !cls && r.width < 0.06f,
                "H3 sniper uses its kit-measured glass row, not the class row");
            Check(ClassRow(GameTitle::Halo3, "fp_battle_rifle").width == 0.085f,
                "battle rifle class is the rifle-class lens placement");
            Check(ScopeLensPlacement(&br, r, cls) && !cls, "H3 battle rifle uses its kit row (low confidence)");
            Check(ScopeLensPlacement(&rocket, r, cls) && cls && r.width == 0.120f && r.up == 0.180f,
                "rocket launcher gets the heavy-class lens placement");
            Check(!ScopeLensPlacement(&ar, r, cls) && !ScopeLensPlacement(&unknown, r, cls) &&
                      !ScopeLensPlacement(nullptr, r, cls),
                "non-optic and unknown weapons get no class placement");
            Check(LensClassOf("fp_beam_rifle") == LensClass::Sniper &&
                      LensClassOf("fp_spartan_laser") == LensClass::Heavy &&
                      LensClassOf("fp_covenant_carbine") == LensClass::Rifle &&
                      LensClassOf(nullptr) == LensClass::Rifle,
                "lens classes from the model name");
        }
        Check(weapon_profile::ScopeLensRowIn(rows, 1, GameTitle::Halo3, "fp_sniper_rifle") == &rows[0] &&
                  !weapon_profile::ScopeLensRowIn(rows, 1, GameTitle::HaloReach, "fp_sniper_rifle") &&
                  !weapon_profile::ScopeLensRowIn(rows, 1, GameTitle::Halo3, "fp_battle_rifle") &&
                  !weapon_profile::ScopeLensRowFor(GameTitle::Halo3, "fp_sniper_rifle") &&
                  !weapon_profile::ScopeLensRowIn(nullptr, 1, GameTitle::Halo3, "x"),
            "a weapon lens row matches only its own title and model; none ship until captured");
    }
    CheckUnit(121);
    {
        // Product resolution can never enable inverse-neck after the
        // 2026-09-27 head-turn sway correction retirement. Standard rear
        // references 0/1/2 and Plus rear reference 3 all resolve NK-off with
        // zeroed knobs and an inactive diagnostic capture family.
        for (const int rearReference : {0, 1, 2, 3})
        {
            Config product{};
            product.virtual_stock = true;
            product.virtual_stock_rear_reference = rearReference;
            const VirtualStockAimSettings resolved =
                VirtualStockAimSettingsFromConfig(product, HybridDiagnosticOverride::Normal);
            Check(!resolved.hybridInverseNeckEnabled &&
                      resolved.hybridInverseNeckStrength == kVirtualStockHybridInverseNeckStrengthDefault &&
                      resolved.hybridInverseNeckForwardM == kVirtualStockHybridInverseNeckForwardDefaultM &&
                      resolved.hybridInverseNeckUpM == kVirtualStockHybridInverseNeckUpDefaultM &&
                      resolved.hybridInverseNeckLateralM == kVirtualStockHybridInverseNeckLateralDefaultM &&
                      !VirtualStockHeadTurnCorrectionFamilyActive(resolved),
                "product Virtual Stock resolution keeps inverse-neck disabled and the capture family inactive");
        }
        Config vsOff{};
        vsOff.virtual_stock = false;
        const VirtualStockAimSettings vsOffSettings =
            VirtualStockAimSettingsFromConfig(vsOff, HybridDiagnosticOverride::Normal);
        Check(!vsOffSettings.hybridInverseNeckEnabled && !VirtualStockHeadTurnCorrectionFamilyActive(vsOffSettings),
            "Virtual Stock OFF leaves inverse-neck disabled with no active capture family");
        Config planted{};
        planted.virtual_stock = true;
        planted.virtual_stock_rear_reference = 0;
        planted.virtual_stock_standard_strength = 0.95f;
        planted.virtual_stock_proximity_release = true;
        planted.virtual_stock_proximity_full_m = 0.270f;
        planted.virtual_stock_proximity_release_m = 0.425f;
        const VirtualStockAimSettings plantedCentre =
            VirtualStockAimSettingsFromConfig(planted, HybridDiagnosticOverride::Normal);
        Check(!plantedCentre.virtualStockProximityRelease && plantedCentre.virtualStockStrength == 0.95f &&
                  plantedCentre.virtualStockRearReference == 0 &&
                  plantedCentre.hybridDiagnosticOverride == HybridDiagnosticOverride::Normal,
            "planted Standard Centre ignores legacy proximity with .95 strength and Normal diagnostic");
        planted.virtual_stock_rear_reference = 1;
        const VirtualStockAimSettings plantedShoulder =
            VirtualStockAimSettingsFromConfig(planted, HybridDiagnosticOverride::Normal);
        Check(!plantedShoulder.virtualStockProximityRelease && plantedShoulder.virtualStockStrength == 0.95f &&
                  plantedShoulder.virtualStockRearReference == 1,
            "planted Standard Shoulder ignores legacy proximity with .95 strength");
        planted.virtual_stock_proximity_full_m = 0.31f;
        planted.virtual_stock_proximity_release_m = 0.57f;
        const VirtualStockAimSettings plantedVaried =
            VirtualStockAimSettingsFromConfig(planted, HybridDiagnosticOverride::Normal);
        Check(!plantedVaried.virtualStockProximityRelease && plantedVaried.virtualStockProximityFullM == 0.31f &&
                  plantedVaried.virtualStockProximityReleaseM == 0.57f,
            "persisted legacy thresholds stay stored but dormant for planted Standard");
    }
    CheckUnit(225);
    ScopeZoomController runtimeZoom;
    Check(std::fabs(runtimeZoom.Update(true, 1.0f, 0.5f, 8.0f) - 8.0f) < 1e-5f,
        "Opening the scope restores configured zoom before applying stick input");
    Check(std::fabs(runtimeZoom.Update(true, 1.0f, 0.10f, 8.0f) - 9.0f) < 1e-5f,
        "Right-stick up increases runtime scope zoom");
    Check(std::fabs(runtimeZoom.Update(true, -1.0f, 0.10f, 8.0f) - 8.0f) < 1e-5f,
        "Right-stick down decreases runtime scope zoom");
    Check(std::fabs(runtimeZoom.Update(true, 0.15f, 0.10f, 8.0f) - 8.0f) < 1e-5f,
        "Scope zoom ignores right-stick deadzone noise");
    runtimeZoom.Update(false, 0.0f, 0.0f, 8.0f);
    Check(std::fabs(runtimeZoom.Update(true, 0.0f, 0.0f, 12.0f) - 12.0f) < 1e-5f,
        "Reopening the scope resets temporary zoom to the configured default");

    ScopeZoomResolver zoomResolver;
    zoomResolver.RequestToggle();
    Check(!zoomResolver.Update(true, false) && zoomResolver.Update(true, false),
        "A weapon with no native zoom opens the fallback scope after detection");
    zoomResolver.RequestToggle();
    Check(zoomResolver.Update(true, false) && !zoomResolver.Update(true, false),
        "A second non-zoom weapon click closes the fallback scope");
    zoomResolver.Reset();
    Check(zoomResolver.Update(true, true), "Halo native zoom immediately owns scope visibility");
    zoomResolver.RequestToggle();
    Check(zoomResolver.Update(true, true), "A second authored zoom stage keeps the scope visible");
    zoomResolver.RequestToggle();
    Check(!zoomResolver.Update(true, false) && !zoomResolver.Update(true, false),
        "Leaving Halo native zoom closes without enabling fallback");
    zoomResolver.Reset();
    zoomResolver.Update(true, true);
    Check(!zoomResolver.Update(true, false), "Native zoom can close before its R3 release is observed");
    zoomResolver.RequestToggle();
    Check(!zoomResolver.Update(true, false) && !zoomResolver.Update(true, false),
        "The late release from native zoom-off is not mistaken for fallback");

    PauseLevelRecovery pauseRecovery;
    Check(!pauseRecovery.Update(true, false, false), "Pause recovery stays armed during an ordinary pause");
    Check(!pauseRecovery.Update(true, true, false), "Pause recovery records loading without resuming early");
    Check(pauseRecovery.Update(true, false, true), "Pause recovery restores 3D when restarted level becomes stable");
    Check(!pauseRecovery.Update(true, false, true), "Pause recovery fires only once per loading gap");
    pauseRecovery.Update(true, true, false);
    Check(!pauseRecovery.Update(false, false, true), "Leaving pause resets an incomplete restart recovery");

    const float rayOrigin[3] = {0.0f, 0.0f, 0.0f};
    const float rayForward[3] = {0.0f, 0.0f, -1.0f};
    MenuPointerHit hit = IntersectMenuQuad(rayOrigin, rayForward, 1.2f, 1.1f, 0.825f, -0.08f);
    Check(hit.hit && hit.u == 0.5f, "Forward controller ray hits the menu center column");
    const float rayAway[3] = {0.0f, 0.0f, 1.0f};
    Check(!IntersectMenuQuad(rayOrigin, rayAway, 1.2f, 1.1f, 0.825f, -0.08f).hit,
        "Controller rays pointing away from the menu miss");

    // The grab handle can slide the panel sideways, so the pointer must follow
    // it. A forward ray that centred the old fixed panel has to miss a panel
    // pushed a full width to the right, and hit its left edge exactly.
    Check(!IntersectMenuQuad(rayOrigin, rayForward, 1.2f, 1.1f, 0.825f, -0.08f, 1.1f).hit,
        "A panel moved a full width aside is no longer under a straight-ahead ray");
    const MenuPointerHit offsetHit = IntersectMenuQuad(rayOrigin, rayForward, 1.2f, 1.1f, 0.825f, -0.08f, 0.55f);
    Check(
        offsetHit.hit && offsetHit.u == 0.0f, "A panel moved half a width right puts its left edge on the forward ray");
    // Passing centerX explicitly must match the defaulted call exactly, and the
    // vertical offset still applies: the panel hangs below the eye line, so a
    // level ray lands above its middle.
    const MenuPointerHit offsetCenter = IntersectMenuQuad(rayOrigin, rayForward, 1.2f, 1.1f, 0.825f, -0.08f, 0.0f);
    Check(offsetCenter.hit && offsetCenter.u == hit.u && offsetCenter.v == hit.v,
        "An explicit zero side offset matches the defaulted panel exactly");
    Check(offsetCenter.v > 0.0f && offsetCenter.v < 0.5f,
        "A level ray lands above the middle of a panel hung below the eye line");
    Check(BlendXInputMotors(0, 0) == 0.0f, "Zero XInput rumble stops haptics");
    Check(BlendXInputMotors(65535, 65535) == 1.0f, "Both full XInput motors produce full portable haptics");
    const float lowOnly = BlendXInputMotors(65535, 0);
    const float highOnly = BlendXInputMotors(0, 65535);
    Check(lowOnly > highOnly && highOnly > 0.0f, "Both motor bands contribute to the blended haptic amplitude");

    Check(NormalizeVirtualXInputSetStateResult(ERROR_DEVICE_NOT_CONNECTED, 0, true) == ERROR_SUCCESS,
        "Virtual slot 0 stays connected when title policy suppresses haptics");
    Check(NormalizeVirtualXInputSetStateResult(ERROR_DEVICE_NOT_CONNECTED, 1, true) == ERROR_DEVICE_NOT_CONNECTED,
        "Foreign XInput slots preserve the underlying SetState result");

    // Peak-hold haptics: a gunfire pulse that rose and fell between two VR
    // frame samples must still fire once, a sustained rumble must persist,
    // and out-of-range inputs must clamp.
    const HapticPeakSample onePulse = SampleHapticPeak(0.8f, 0.0f);
    Check(onePulse.apply == 0.8f, "A rumble pulse that already returned to zero still applies its peak");
    Check(onePulse.carry == 0.0f, "A one-shot rumble pulse does not persist after it is applied");
    const HapticPeakSample sustained = SampleHapticPeak(0.3f, 0.6f);
    Check(sustained.apply == 0.6f && sustained.carry == 0.6f,
        "A sustained rumble above the stale peak applies and carries forward");
    const HapticPeakSample clamped = SampleHapticPeak(1.5f, -0.5f);
    Check(
        clamped.apply == 1.0f && clamped.carry == 0.0f, "Peak-hold haptic samples clamp to the [0,1] amplitude range");
    Check(std::fabs(MergeHapticAmplitude(0.7f, 0.2f) - 0.7f) < 1.0e-6f &&
              std::fabs(MergeHapticAmplitude(0.1f, 0.4f) - 0.4f) < 1.0e-6f &&
              std::fabs(MergeHapticAmplitude(-1.0f, 2.0f) - 1.0f) < 1.0e-6f,
        "Per-hand contact haptics merge by clamped maximum without replacing game rumble");

    // Halo 4 world-contact resolution: no hit accepts the controller target; a
    // hit backs the wrist off by the configured skin in world-scaled units;
    // malformed engine output fails closed for this optional feature only.
    const float collisionStart[3]{0.0f, 0.0f, 0.0f};
    const float collisionDesired[3]{1.0f, 0.0f, 0.0f};
    const Halo4WorldCollisionResolution freeMovement =
        Halo4ResolveWorldCollision(collisionStart, collisionDesired, false, 1.0f, 1.0f, 0.1f);
    Check(freeMovement.valid && !freeMovement.contact && std::fabs(freeMovement.accepted[0] - 1.0f) < 1.0e-6f &&
              std::fabs(freeMovement.correction[0]) < 1.0e-6f,
        "Halo 4 wrist sweep accepts an unobstructed tracked target");
    const Halo4WorldCollisionResolution blockedMovement =
        Halo4ResolveWorldCollision(collisionStart, collisionDesired, true, 0.5f, 1.0f, 0.1f);
    Check(blockedMovement.valid && blockedMovement.contact && std::fabs(blockedMovement.accepted[0] - 0.4f) < 1.0e-6f &&
              std::fabs(blockedMovement.correction[0] + 0.6f) < 1.0e-6f,
        "Halo 4 wrist sweep resolves before the impact by a world-scaled skin");
    Check(!Halo4ResolveWorldCollision(collisionStart, collisionDesired, true, 1.5f, 1.0f).valid,
        "Halo 4 wrist sweep rejects an invalid engine hit fraction");
    const Halo2WorldCollisionResolution halo2Blocked =
        Halo2ResolveWorldCollision(collisionStart, collisionDesired, true, 0.5f, 1.0f, 0.1f);
    Check(halo2Blocked.valid && halo2Blocked.contact && std::fabs(halo2Blocked.accepted[0] - 0.4f) < 1.0e-6f &&
              std::fabs(halo2Blocked.correction[0] + 0.6f) < 1.0e-6f,
        "Halo 2 final-packet sweep resolves before native contact by a scaled skin");
    const float authoredVolumePoints[][3]{{-0.2f, 0.0f, 0.0f}, {0.3f, 0.0f, 0.0f}, {0.0f, -0.4f, 0.0f},
        {0.0f, 0.5f, 0.0f}, {0.0f, 0.0f, -0.6f}, {0.0f, 0.0f, 0.7f}};
    float selectedVolume[kHalo4WorldCollisionExtremaCount][3]{};
    const int selectedVolumeCount = Halo4SelectWorldCollisionExtrema(collisionStart, &authoredVolumePoints[0][0],
        static_cast<int>(std::size(authoredVolumePoints)), selectedVolume, static_cast<int>(std::size(selectedVolume)));
    Check(selectedVolumeCount == kHalo4WorldCollisionExtremaCount && std::fabs(selectedVolume[1][0] + 0.2f) < 1.0e-6f &&
              std::fabs(selectedVolume[6][2] - 0.7f) < 1.0e-6f,
        "Halo 4 contact volume selects the authored root and six model extrema");
    float halo2Selected[kHalo2WorldCollisionSampleCount][3]{};
    Check(Halo2SelectWorldCollisionExtrema(collisionStart, &authoredVolumePoints[0][0],
              static_cast<int>(std::size(authoredVolumePoints)), halo2Selected,
              static_cast<int>(std::size(halo2Selected))) == kHalo2WorldCollisionSampleCount &&
              std::fabs(halo2Selected[1][0] + 0.2f) < 1.0e-6f && std::fabs(halo2Selected[6][2] - 0.7f) < 1.0e-6f,
        "Halo 2 visible packets select stable root and model extrema without invented weapon dimensions");
    const float degenerateVolumePoint[3]{0.1f, 0.2f, 0.3f};
    float stableVolume[kHalo4WorldCollisionExtremaCount][3]{};
    Check(Halo4SelectWorldCollisionExtrema(collisionStart, degenerateVolumePoint, 1, stableVolume,
              static_cast<int>(std::size(stableVolume))) == kHalo4WorldCollisionExtremaCount &&
              std::fabs(stableVolume[1][0] - stableVolume[6][0]) < 1.0e-6f,
        "Halo 4 authored extrema retain fixed semantic slots when extrema coincide");
    const Halo4WeaponCollisionBounds* assaultRifleBounds = Halo4FindWeaponCollisionBounds(0x1814181Cu);
    Check(kHalo4WeaponCollisionBoundsCount == 39 && assaultRifleBounds &&
              std::fabs(assaultRifleBounds->minimum[0] + 0.0935352f) < 1.0e-6f &&
              std::fabs(assaultRifleBounds->maximum[0] - 0.23352f) < 1.0e-6f &&
              !Halo4FindWeaponCollisionBounds(0xFFFFFFFFu),
        "Halo 4 weapon bounds resolve the exact H4EK model checksum and fail open when unknown");
    const LegacyWeaponCollisionBounds* halo3AssaultRifleBounds =
        LegacyFindWeaponCollisionBounds(kH3OdstWeaponCollisionBounds, 0x1C080C11u);
    const LegacyWeaponCollisionBounds* odstAssaultRifleBounds =
        LegacyFindWeaponCollisionBounds(kH3OdstWeaponCollisionBounds, 0x1B010D0Cu);
    const LegacyWeaponCollisionBounds* reachDmrBounds =
        LegacyFindWeaponCollisionBounds(kReachWeaponCollisionBounds, 0x110D171Eu);
    Check(std::size(kH3OdstWeaponCollisionBounds) == 32 && std::size(kReachWeaponCollisionBounds) == 29 &&
              halo3AssaultRifleBounds && odstAssaultRifleBounds && reachDmrBounds &&
              std::fabs(halo3AssaultRifleBounds->minimum[2] + 0.0924675f) < 1.0e-6f &&
              std::fabs(odstAssaultRifleBounds->maximum[0] - 0.474487f) < 1.0e-6f &&
              std::fabs(reachDmrBounds->maximum[0] - 0.373775f) < 1.0e-6f &&
              !LegacyFindWeaponCollisionBounds(kH3OdstWeaponCollisionBounds, 0xFFFFFFFFu) &&
              !LegacyFindWeaponCollisionBounds(kReachWeaponCollisionBounds, 0xFFFFFFFFu),
        "H3/ODST/Reach weapon bounds select exact editing-kit checksums and fail open when unknown");
    Check(LegacyWeaponCollisionCacheCanSupply(1000, 900, 7, 7) &&
              LegacyWeaponCollisionCacheCanSupply(1000, 850, 7, 7) &&
              !LegacyWeaponCollisionCacheCanSupply(1000, 849, 7, 7) &&
              !LegacyWeaponCollisionCacheCanSupply(1000, 1001, 7, 7) &&
              !LegacyWeaponCollisionCacheCanSupply(1000, 900, 7, 8) &&
              !LegacyWeaponCollisionCacheCanSupply(1000, 900, 0, 0),
        "legacy weapon identity cache is generation exact, bounded, and expires to hand-only");
    Check(LegacyMappedWeaponRootIsUsable(17, 32, 0x1000, 0x1000) &&
              !LegacyMappedWeaponRootIsUsable(-1, 32, 0x1000, 0x1000) &&
              !LegacyMappedWeaponRootIsUsable(32, 32, 0x1000, 0x1000) &&
              !LegacyMappedWeaponRootIsUsable(17, 65, 0x1000, 0x1000) &&
              !LegacyMappedWeaponRootIsUsable(17, 32, 0x1000, 0x2000) && !LegacyMappedWeaponRootIsUsable(0, 32, 0, 0),
        "mapped weapon roots require the renderer's exact source graph and bounded map index");
    CheckUnit(232);
    if (assaultRifleBounds)
    {
        const float identityBasis[9]{1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
        const float modelTranslation[3]{10.0f, 20.0f, 30.0f};
        const float volumeRoot[3]{1.0f, 2.0f, 3.0f};
        const float volumeWrist[3]{0.5f, 1.5f, 2.5f};
        float weaponSamples[kHalo4WeaponCollisionBoundsSampleCount][3]{};
        const int weaponSampleCount = Halo4BuildWeaponCollisionBoundsSamples(*assaultRifleBounds, 2.0f, identityBasis,
            modelTranslation, volumeRoot, volumeWrist, weaponSamples, static_cast<int>(std::size(weaponSamples)));
        Check(weaponSampleCount == kHalo4WeaponCollisionBoundsSampleCount &&
                  std::fabs(weaponSamples[0][0] - (10.5f + 2.0f * assaultRifleBounds->minimum[0])) < 1.0e-6f &&
                  std::fabs(weaponSamples[7][0] - (10.5f + 2.0f * assaultRifleBounds->maximum[0])) < 1.0e-6f &&
                  std::fabs(weaponSamples[8][1] -
                            (20.5f + assaultRifleBounds->minimum[1] + assaultRifleBounds->maximum[1])) < 1.0e-6f,
            "Halo 4 weapon bounds publish eight corners and six face centres in the carried root frame");
    }
    CheckUnit(234);
    const float pushPrevious[3]{0.0f, 0.0f, 0.0f};
    const float pushDesired[3]{1.0f, 0.0f, 0.0f};
    float pushVelocity[3]{};
    Check(Halo4BuildWorldCollisionPushVelocity(pushPrevious, pushDesired, 10, 0.5f, pushVelocity) &&
              std::fabs(pushVelocity[0] - 1.0f) < 1.0e-6f,
        "Halo 4 dynamic contact derives and world-scale clamps a finite push velocity");
    Check(!Halo4BuildWorldCollisionPushVelocity(pushPrevious, pushDesired, 0, 0.5f, pushVelocity),
        "Halo 4 dynamic contact refuses a zero-time velocity sample");
    const float meleePreviousDesired[3]{0.0f, 0.0f, 0.0f};
    const float meleeDesired[3]{0.06f, 0.0f, 0.0f};
    const float meleeSpeed = Halo4PhysicalMeleeSwingSpeedMetresPerSecond(meleePreviousDesired, meleeDesired, 100, 0.5f);
    Check(std::fabs(meleeSpeed - 1.2f) < 1.0e-6f && Halo4PhysicalMeleeContactQualifies(42, 7, meleeSpeed, 1.2f),
        "Halo 4 physical melee converts raw authored sample travel to metres per second");
    const float velocityPrevious[3]{0.0f, 0.0f, 0.0f};
    const float velocityCurrent[3]{0.02f, -0.01f, 0.0f};
    float poseVelocity[3]{};
    Check(Halo4DeriveControllerPoseVelocity(velocityPrevious, velocityCurrent, 10000000ll, poseVelocity) &&
              std::fabs(poseVelocity[0] - 2.0f) < 1.0e-6f && std::fabs(poseVelocity[1] + 1.0f) < 1.0e-6f &&
              !Halo4DeriveControllerPoseVelocity(velocityPrevious, velocityCurrent, 0, poseVelocity),
        "physical melee derives bounded controller velocity from predicted-time pose deltas");
    const float advertisedZeroVelocity[3]{};
    const float nativeVelocity[3]{1.5f, 0.0f, 0.0f};
    float selectedVelocity[3]{};
    bool usedPoseVelocity = false;
    Check(Halo4SelectControllerLinearVelocity(
              true, advertisedZeroVelocity, true, poseVelocity, selectedVelocity, usedPoseVelocity) &&
              usedPoseVelocity && std::fabs(selectedVelocity[0] - 2.0f) < 1.0e-6f,
        "pose-delta velocity replaces WMR runtime advertised-zero velocity");
    Check(Halo4SelectControllerLinearVelocity(
              true, nativeVelocity, true, poseVelocity, selectedVelocity, usedPoseVelocity) &&
              !usedPoseVelocity && std::fabs(selectedVelocity[0] - 1.5f) < 1.0e-6f,
        "meaningful native OpenXR velocity remains preferred for accepted controller paths");
    Check(!Halo4PhysicalMeleeContactQualifies(-1, 7, meleeSpeed, 1.2f) &&
              !Halo4PhysicalMeleeContactQualifies(7, 7, meleeSpeed, 1.2f) &&
              !Halo4PhysicalMeleeContactQualifies(42, 7, meleeSpeed, 1.21f),
        "Halo 4 physical melee requires a non-player dynamic contact above threshold");
    Check(Halo4PhysicalMeleeSwingSpeedMetresPerSecond(meleeDesired, meleeDesired, 100, 0.5f) == 0.0f &&
              Halo4PhysicalMeleeSwingSpeedMetresPerSecond(meleePreviousDesired, meleeDesired, 0, 0.5f) < 0.0f,
        "Halo 4 physical melee ignores stationary wall pressure and invalid timing");
    const float runtimeVelocity[3]{0.0f, 3.0f, 4.0f};
    Check(std::fabs(Halo4PhysicalMeleeVelocityMagnitude(runtimeVelocity) - 5.0f) < 1.0e-6f,
        "Halo 4 physical melee consumes OpenXR tracking-space velocity in metres per second");
    const Halo4PhysicalMeleeVelocityDecision firstSwing =
        Halo4UpdatePhysicalMeleeVelocityLatch(true, 1.2f, 1.2f, false);
    const Halo4PhysicalMeleeVelocityDecision heldSwing = Halo4UpdatePhysicalMeleeVelocityLatch(true, 2.0f, 1.2f, true);
    const Halo4PhysicalMeleeVelocityDecision releasedSwing =
        Halo4UpdatePhysicalMeleeVelocityLatch(true, 0.60f, 1.2f, true);
    const Halo4PhysicalMeleeVelocityDecision invalidSwing =
        Halo4UpdatePhysicalMeleeVelocityLatch(false, 9.0f, 1.2f, true);
    const Halo4PhysicalMeleeVelocityDecision fiveMetreSwing =
        Halo4UpdatePhysicalMeleeVelocityLatch(true, 5.0f, 5.0f, false);
    // The accepted threshold range is the config's (0.3-10 m/s). It used to stop
    // at 5 m/s, so any configured threshold above 5 silently disabled H4 swing melee.
    const Halo4PhysicalMeleeVelocityDecision aboveOldCeiling =
        Halo4UpdatePhysicalMeleeVelocityLatch(true, 5.1f, 5.1f, false);
    const Halo4PhysicalMeleeVelocityDecision configMaximum =
        Halo4UpdatePhysicalMeleeVelocityLatch(true, 10.0f, 10.0f, false);
    const Halo4PhysicalMeleeVelocityDecision overMaximumThreshold =
        Halo4UpdatePhysicalMeleeVelocityLatch(true, 10.1f, 10.1f, false);
    Check(firstSwing.trigger && firstSwing.latched && !heldSwing.trigger && heldSwing.latched &&
              !releasedSwing.trigger && !releasedSwing.latched && !invalidSwing.trigger && !invalidSwing.latched &&
              fiveMetreSwing.trigger && fiveMetreSwing.latched && aboveOldCeiling.trigger && configMaximum.trigger &&
              !overMaximumThreshold.trigger && !overMaximumThreshold.latched,
        "physical melee fires once per tracked swing, accepts the full config threshold range, and rearms below hysteresis");
    Check(kHalo4PhysicalMeleeRequiredSpeedMin == kPhysicalMeleeSpeedMin &&
              kHalo4PhysicalMeleeRequiredSpeedMax == kPhysicalMeleeSpeedMax,
        "Halo 4 melee threshold range matches the config clamp");
    Check(
        Halo4PhysicalMeleeContactQualifies(42, 7, 6.0f, 5.0f) && !Halo4PhysicalMeleeContactQualifies(42, 7, 4.9f, 5.0f),
        "Halo 4 contact melee accepts the default 5 m/s threshold (was capped at 3)");
    const float ordinaryMovement[3]{0.40f, 0.0f, 0.0f};
    const float teleportMovement[3]{0.60f, 0.0f, 0.0f};
    Check(!Halo4WorldCollisionMovementIsTeleport(collisionStart, ordinaryMovement, 0.33f) &&
              Halo4WorldCollisionMovementIsTeleport(collisionStart, teleportMovement, 0.33f),
        "Halo 4 collision reseeds across a 1.5 metre world-scaled tracking jump");
    Check(NormalizeVirtualXInputSetStateResult(ERROR_DEVICE_NOT_CONNECTED, 0, false) == ERROR_DEVICE_NOT_CONNECTED,
        "An invalid vibration request preserves the underlying SetState result");

    Check(!IsMaterialFramePeriodTransition(8333333, 8333334),
        "OpenXR nanosecond period jitter does not trigger a pacing capture");
    Check(IsMaterialFramePeriodTransition(8333333, 16666667),
        "A 120-to-60 runtime cadence change triggers a pacing capture");
    Check(IsMaterialFramePeriodTransition(11111111, 22222222),
        "A 90-to-45 runtime cadence change triggers a pacing capture");
    Check(IsMaterialFramePeriodTransition(6944444, 13888889),
        "A 144-to-72 runtime cadence change triggers a pacing capture");
    Check(!IsMaterialFramePeriodTransition(8333333, 8403361),
        "A small runtime-period adjustment does not masquerade as a cadence flip");
    Check(ShouldReleaseFrameWaitWorkerBeforeBegin(true, true),
        "A claimed worker packet releases the next wait before xrBeginFrame");
    Check(!ShouldReleaseFrameWaitWorkerBeforeBegin(true, false),
        "A missing worker packet never releases another wait before xrBeginFrame");
    Check(!ShouldReleaseFrameWaitWorkerBeforeBegin(false, true),
        "No wait-worker release occurs when the worker is unavailable");
    Check(ClassifyFrameWaitPermit(7, 6) == FrameWaitPermit::Park,
        "The worker remains parked until its exact packet sequence is released");
    Check(ClassifyFrameWaitPermit(7, 7) == FrameWaitPermit::StartNextWait,
        "An exact sequence acknowledgement permits one subsequent wait");
    Check(ClassifyFrameWaitPermit(7, 8) == FrameWaitPermit::Fault &&
              ClassifyFrameWaitPermit(0, 0) == FrameWaitPermit::Fault,
        "Skipped or invalid wait generations fault instead of issuing a wait");
    Check(IsExpectedNextFrameWaitDispatch(7, 8), "Begin admission observes the exact subsequent worker dispatch");
    Check(!IsExpectedNextFrameWaitDispatch(7, 7) && !IsExpectedNextFrameWaitDispatch(7, 9) &&
              !IsExpectedNextFrameWaitDispatch(0, 1) && !IsExpectedNextFrameWaitDispatch(UINT64_MAX, 0),
        "Begin admission rejects stale, skipped, invalid, or wrapped dispatches");

    // Reach observer-camera head-lock: the call-site classifier decides which
    // consumers get taken off the hand. Getting the world-render index wrong
    // would double-apply head-look to the accepted Reach 3D path.
    Check(ReachClassifyObserverCameraReturn(0x0026C2DE) == kReachObserverCameraWorldSite,
        "The world render camera return address maps to the world site");
    Check(ReachClassifyObserverCameraReturn(0x002E1525) == kReachObserverCameraChudSite,
        "The CHUD projection return address maps to the CHUD site");
    Check(kReachObserverCameraWorldSite != kReachObserverCameraChudSite, "The world and CHUD sites are distinct");
    Check(ReachClassifyObserverCameraReturn(0x0025B404) == 0 && ReachClassifyObserverCameraReturn(0x0025D406) == 1 &&
              ReachClassifyObserverCameraReturn(0x0026FA47) == 3 &&
              ReachClassifyObserverCameraReturn(0x0026FB0E + 5) == 4,
        "Every measured observer-camera call site maps to its own index");
    Check(ReachClassifyObserverCameraReturn(0x0026C2DD) < 0 && ReachClassifyObserverCameraReturn(0x0026C2DF) < 0 &&
              ReachClassifyObserverCameraReturn(0) < 0,
        "An off-by-one or unknown return address is never classified as a site");
    CheckUnit(240);
    {
        // The upload path's source-view cache. The defect this replaces was a
        // ONE-entry cache keyed on the source texture pointer: a frame that
        // publishes two eyes from two different textures missed on every
        // single call and asked the device for a new view each time.

        // Stand-ins for the resources and the views made from them. Only the
        // addresses matter; the cache never dereferences either.
        int eyeLeft = 0, eyeRight = 0, menu = 0, screen = 0;
        int viewLeft = 0, viewRight = 0, viewMenu = 0, viewScreen = 0;

        ViewCacheTable<32> cache;

        // A cold frame misses once per distinct source, which is unavoidable.
        const bool coldMisses = !cache.Find(&eyeLeft).hit && !cache.Find(&eyeRight).hit;
        cache.Insert(&eyeLeft, &viewLeft);
        cache.Insert(&eyeRight, &viewRight);

        // THE REGRESSION THIS LOCKS DOWN: alternating between the two eyes
        // must hit every time. The old single slot returned a miss on all
        // four of these lookups.
        bool alternatingHits = true;
        for (int frame = 0; frame < 4; ++frame)
        {
            const ViewCacheLookup left = cache.Find(&eyeLeft);
            const ViewCacheLookup right = cache.Find(&eyeRight);
            alternatingHits =
                alternatingHits && left.hit && left.view == &viewLeft && right.hit && right.view == &viewRight;
        }

        // Adding the menu and the screen quad to the same frame does not
        // evict the eyes, which is what a one-slot cache could not manage.
        cache.Insert(&menu, &viewMenu);
        cache.Insert(&screen, &viewScreen);
        const bool busyFrameHolds = cache.Contains(&eyeLeft) && cache.Contains(&eyeRight) && cache.Contains(&menu) &&
                                    cache.Contains(&screen) && cache.Size() == 4;

        Check(coldMisses && alternatingHits && busyFrameHolds,
            "Source-view cache holds every source a frame touches, so "
            "alternating eyes stop recreating a view per eye per frame");

        // Nothing is displaced while capacity remains, so steady-state play
        // performs no releases at all.
        ViewCacheTable<32> steady;
        int sources[6]{};
        int views[6]{};
        bool noDisplacement = true;
        for (int i = 0; i < 6; ++i)
            noDisplacement = noDisplacement && steady.Insert(&sources[i], &views[i]) == nullptr;
        // Replay ten frames over that working set: all hits, no misses.
        steady.ResetStats();
        for (int frame = 0; frame < 10; ++frame)
            for (int i = 0; i < 6; ++i)
                (void)steady.Find(&sources[i]);
        const bool steadyStateIsFree =
            steady.Stats().hits == 60 && steady.Stats().misses == 0 && steady.Stats().evictions == 0;
        Check(noDisplacement && steadyStateIsFree, "A working set inside capacity reaches a steady state with zero "
                                                   "view creation and zero eviction");

        // Eviction, exercised at a small capacity: the least recently USED
        // entry goes, not the least recently inserted. Touching a stale entry
        // must save it.
        ViewCacheTable<3> tight;
        int a = 0, b = 0, c = 0, d = 0;
        int va = 0, vb = 0, vc = 0, vd = 0;
        tight.Insert(&a, &va);
        tight.Insert(&b, &vb);
        tight.Insert(&c, &vc);
        (void)tight.Find(&a); // `a` is now the freshest, `b` oldest
        void* evicted = tight.Insert(&d, &vd);
        const bool lru = evicted == &vb && !tight.Contains(&b) && tight.Contains(&a) && tight.Contains(&c) &&
                         tight.Contains(&d) && tight.Size() == 3 && tight.Stats().evictions == 1;
        Check(lru, "The cache evicts the least recently used view and keeps "
                   "the resources the current frame is still touching");

        // Re-inserting the same key hands back the previous view to release,
        // and never grows the table - otherwise a resource that legitimately
        // gets a fresh view would leak one every time.
        ViewCacheTable<4> replace;
        int key = 0, first = 0, second = 0;
        replace.Insert(&key, &first);
        void* superseded = replace.Insert(&key, &second);
        const bool replaced = superseded == &first && replace.Size() == 1 && replace.Find(&key).view == &second;

        // Forget() is how the owner drops a resource it is about to destroy,
        // which is what stops the table pinning a dead texture alive.
        void* forgotten = replace.Forget(&key);
        const bool forgets =
            forgotten == &second && replace.Size() == 0 && !replace.Find(&key).hit && replace.Forget(&key) == nullptr;
        Check(replaced && forgets, "Re-inserting a key returns the superseded view, and Forget drops "
                                   "a resource the owner is recreating");

        // Guards. A failed view creation must not be stored: a poisoned slot
        // would report a hit and hand back nothing, which is worse than the
        // miss it replaced.
        ViewCacheTable<4> guarded;
        int live = 0, view = 0;
        const bool rejectsNulls = guarded.Insert(&live, nullptr) == nullptr && !guarded.Find(&live).hit &&
                                  guarded.Size() == 0 && guarded.Insert(nullptr, &view) == &view &&
                                  !guarded.Find(nullptr).hit && !guarded.Contains(nullptr) &&
                                  guarded.Forget(nullptr) == nullptr;

        // Teardown drains every entry exactly once, so nothing leaks and
        // nothing is released twice.
        guarded.Insert(&live, &view);
        int extra = 0, extraView = 0;
        guarded.Insert(&extra, &extraView);
        int drained = 0;
        while (guarded.TakeAny() != nullptr)
            ++drained;
        const bool drains = drained == 2 && guarded.Size() == 0;
        Check(rejectsNulls && drains, "A failed creation is never cached, and teardown drains each held "
                                      "view exactly once");
    }

    CheckUnit(245);
    Check(MenuSliderStep("%.0f%%") == 1.0 && std::fabs(MenuSliderStep("%.2f m/s") - 0.01) < 1e-12 &&
              std::fabs(MenuSliderStep("%.3f") - 0.001) < 1e-12 && MenuSliderStep("%+.0f px") == 1.0 &&
              MenuSliderStep("%d") == 1.0,
        "menu arrows step the last displayed digit across integer, percent and fractional formats");
    CheckUnit(247);
    Check(std::fabs(MenuSliderNudge(0.333333f, 0, 1, 0.01, 1) - 0.34f) < 1e-6f &&
              std::fabs(MenuSliderNudge(-0.025f, -0.3f, 0.3f, 0.001, -1) + 0.026f) < 1e-6f &&
              MenuSliderNudge(10, 0.3f, 10, 0.01, 1) == 10 && MenuSliderNudge(0.3f, 0.3f, 10, 0.01, -1) == 0.3f,
        "menu nudges round the shown value and preserve both slider endpoints");

    CheckUnit(248);
    // Free two-hand offhand influence F1 display: the stored 0..1
    // authority is shown as a 0..100 percent label, so the 0.50 default reads
    // "50%" and never the raw fraction.
    {
        const float defaultPercent = MenuSliderPercentFromUnit(kTwoHandOffhandInfluenceDefault);
        char influenceLabel[16]{};
        std::snprintf(influenceLabel, sizeof(influenceLabel), "%.0f%%", defaultPercent);
        Check(defaultPercent == 50.0f && std::strcmp(influenceLabel, "50%") == 0 &&
                  kTwoHandOffhandInfluenceDefault == 0.50f,
            "the free two-hand offhand influence default 0.50 displays as 50 percent, never as the raw fraction");
        Check(MenuSliderPercentFromUnit(0.0f) == 0.0f && MenuSliderPercentFromUnit(0.25f) == 25.0f &&
                  MenuSliderPercentFromUnit(1.0f) == 100.0f,
            "offhand influence 0..1 maps onto a 0..100 percent slider");
        Check(MenuSliderPercentFromUnit(1.5f) == 100.0f && MenuSliderPercentFromUnit(-0.5f) == 0.0f &&
                  MenuSliderPercentFromUnit(std::numeric_limits<float>::quiet_NaN()) == 0.0f &&
                  MenuSliderPercentFromUnit(std::numeric_limits<float>::infinity()) == 0.0f,
            "an out-of-range or non-finite offhand influence percent display clamps safely");
        Check(MenuSliderUnitFromPercent(0.0f) == 0.0f && MenuSliderUnitFromPercent(50.0f) == 0.5f &&
                  MenuSliderUnitFromPercent(100.0f) == 1.0f && MenuSliderUnitFromPercent(101.0f) == 1.0f &&
                  MenuSliderUnitFromPercent(-1.0f) == 0.0f &&
                  MenuSliderUnitFromPercent(std::numeric_limits<float>::quiet_NaN()) == 0.0f,
            "the offhand influence percent slider writes back the exact clamped 0..1 authority");
        Check(MenuSliderUnitFromPercent(defaultPercent) == kTwoHandOffhandInfluenceDefault,
            "the offhand influence round-trips through the percent slider unchanged");
    }

    CheckUnit(-1);
    std::cout << "checks run: " << g_checks << '\n';
    if (g_failures == 0)
        std::cout << "HaloMCCVR core aim scope hud tests passed\n";
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
