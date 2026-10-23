// Weapon interaction rules: body gestures, holster and grab zones, body yaw and snap-in reload.
// Used by config.cpp, grenade_throw_logic.h, weapon_accessory_logic.h, weapon_reload_target.h and
// src/dll/core (game.cpp, input.cpp, menu.cpp, vr.cpp); tested by tests/weapon_interaction_tests.cpp.
// Pure logic: no globals, no game memory, no OpenXR (callers pass tracked poses in).
#pragma once
#include "math_types.h"

#include "runtime_types.h"
#include "weapon_model_catalog.h"
#include "weapon_profiles.h"
#include "feedback_cues.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

// Body gestures are an optional controller-input layer. They never mutate
// inventory, ammo, animation clocks, native tags or camera ownership.
namespace weapon_interaction
{
    inline constexpr unsigned kTitleCount = 6;
    inline constexpr const char* kTitleKeys[kTitleCount]{"halo3", "odst", "reach", "halo4", "ce", "halo2"};
    inline constexpr const char* kTitleNames[kTitleCount]{"Halo 3", "ODST", "Reach", "Halo 4", "Halo CE", "Halo 2"};
    inline constexpr const char* kButtonNames = "X\0Right bumper\0Left bumper\0B\0Y\0A\0Left trigger\0Right trigger\0";
    inline constexpr uint32_t kButtons[]{0x4000, 0x200, 0x100, 0x2000, 0x8000, 0x1000, 1u << 16, 1u << 17};
    inline constexpr int TitleIndex(GameTitle title) noexcept
    {
        const int value = static_cast<int>(title);
        return value >= 1 && value <= 6 ? value - 1 : -1;
    }
    inline constexpr uint32_t Button(int index) noexcept
    {
        return index >= 0 && index < 8 ? kButtons[index] : 0;
    }
    using Vec = mvr_math::Vec3;
    using Quat = mvr_math::Quat;
    inline float Dot(Vec a, Vec b) noexcept
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }
    inline bool Finite(Vec a) noexcept
    {
        return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
    }
    inline bool Normal(Quat q) noexcept
    {
        const float n = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
        return std::isfinite(n) && n > 0.98f && n < 1.02f;
    }
    inline Vec Rotate(Quat q, Vec v) noexcept
    {
        const Vec u{q.x, q.y, q.z};
        const Vec c{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
        return u * (2 * Dot(u, v)) + v * (q.w * q.w - Dot(u, u)) + c * (2 * q.w);
    }
    inline bool Near(Vec a, Vec b, float r) noexcept
    {
        const Vec d = a - b;
        return Dot(d, d) <= r * r;
    }
    inline float Setting(float v, float low, float high, float fallback) noexcept
    {
        return std::isfinite(v) ? std::clamp(v, low, high) : fallback;
    }

    // Where the reserve (holstered) weapon is carried. Unknown keeps the configured
    // holster location; the per-weapon class table (pistol -> hip, long gun ->
    // shoulder) reports through Sample::reserveSlot.
    enum class HolsterSlot : uint8_t
    {
        Unknown,
        Shoulder,
        Hip
    };
    // Pistols ride the hip, long guns the shoulder; anything else makes no claim.
    // Caches the reserve weapon's slot per (title, identity), so the profile
    // lookup runs once per stowed-weapon change, not every frame.
    struct ReserveSlotCache
    {
        template <class Lookup>
        HolsterSlot Get(GameTitle title, uint64_t identity, Lookup lookup)
        {
            if (!valid_ || title != title_ || identity != identity_)
            {
                slot_ = identity ? lookup() : HolsterSlot::Unknown;
                title_ = title;
                identity_ = identity;
                valid_ = true;
            }
            return slot_;
        }

    private:
        GameTitle title_{GameTitle::None};
        uint64_t identity_{};
        HolsterSlot slot_{};
        bool valid_{};
    };
    inline constexpr HolsterSlot SlotFor(weapon_profile::SizeClass size) noexcept
    {
        return size == weapon_profile::SizeClass::OneHanded ? HolsterSlot::Hip
               : size == weapon_profile::SizeClass::Long    ? HolsterSlot::Shoulder
                                                            : HolsterSlot::Unknown;
    }

    // Holster entry tick with a re-trigger dead zone (a 2025 VR shooter update: a slot that
    // buzzes on every pass is as bad as none). One tick per entry; it re-arms once
    // the hand is past the dead zone, or has stayed outside the zone 300 ms.
    struct HolsterTick
    {
        bool armed{true};
        uint64_t outsideSince{};
    };
    inline constexpr uint64_t kHolsterTickRearmMs = 300;
    inline bool StepHolsterTick(HolsterTick& t, bool inside, bool pastDeadZone, uint64_t now) noexcept
    {
        if (inside)
        {
            t.outsideSince = 0;
            if (!t.armed)
                return false;
            t.armed = false;
            return true;
        }
        if (t.armed)
            return false;
        if (pastDeadZone)
        {
            t.armed = true;
            t.outsideSince = 0;
            return false;
        }
        if (!t.outsideSince)
            t.outsideSince = now ? now : 1;
        else if (now >= t.outsideSince && now - t.outsideSince >= kHolsterTickRearmMs)
        {
            t.armed = true;
            t.outsideSince = 0;
        }
        return false;
    }

    struct Settings
    {
        bool reload{}, holsters{}, leftHanded{};
        float pouchDown{0.50f}, zoneRadius{0.20f};
        int pouchLocation{};
        Vec pouchOffset{}; // body-local right, up, forward; right mirrors with handedness
        float holsterRadius{0.20f}, insertRadius{0.13f}, drawDistance{0.25f};
        bool holsterSlide{true}, holsterClick{};
        bool needleShake{};
        bool genericVisual{true};
        float shakeTravel{0.10f};
        // 0 shoulder, 1 hip. A single reserve slot exchanges the two native guns.
        int holsterLocation{};
        uint32_t reloadButton{0x4000}, swapButton{0x8000};
        // Zones follow a smoothed body yaw instead of every head turn.
        bool bodyAnchor{true};
        float zoneHaptic{feedback::Strength(feedback::Cue::HolsterEnter)}; // holster-entry tick strength; 0 disables it
        float zoneHapticRearmM{0.0f}; // re-trigger dead zone past the edge; 0 = any exit re-arms
        // Snap-in reload: take the magazine with the support TRIGGER (default) or,
        // as before, the support grip.
        bool takeWithGrip{};
    };
    struct Sample
    {
        uint64_t now{}, space{};
        uint32_t generation{};
        GameTitle title{GameTitle::None};
        bool ready{}, dualWield{};
        Vec head{}, primary{}, support{};
        Quat headRotation{}, primaryRotation{};
        float primaryGrip{}, supportGrip{};
        float supportTrigger{}; // 0..1; NaN = unavailable
        bool otherAction{};
        uint64_t weaponGraph{};
        bool receiverValid{};
        Vec receiver{};
        Quat supportRotation{};
        HolsterSlot reserveSlot{HolsterSlot::Unknown};
    };

    inline Vec HeldMagazineCenter(const Sample& sample, Quat supportRotation) noexcept
    {
        return sample.support + Rotate(supportRotation, {0, -.015f, -.035f});
    }

    // Title-specific identities from the official kits: CE graph fingerprint,
    // H2 verified compression tuple, and H3/ODST/Reach/H4 model import checksum.
    inline bool NeedleWeapon(GameTitle title, uint64_t graph) noexcept
    {
        const auto* model = weapon_model::Find(title, graph);
        return model && model->needles;
    }
    struct Output
    {
        bool consumePrimary{}, consumeSupport{}, releaseTwoHand{};
        bool reloadRequested{}, swapRequested{}, pickedMagazine{}, grabbedHolster{};
        bool holdingMagazine{};
        float primaryHaptic{}, supportHaptic{};
        uint32_t buttons{};
        uint64_t pulseUntil{};
        Vec bodyForward{};            // horizontal basis the zones used; zero when head-anchored
        bool consumeSupportTrigger{}; // the support trigger is holding a magazine; hide it from the game
    };
    // After a holster draw has completed the weapon grip stays consumed until
    // release, but that hold does not own the other hand. Only an active gesture
    // or a held magazine prevents acquiring the support grip.
    inline bool BlocksSupportGrab(const Output& output) noexcept
    {
        return output.releaseTwoHand || output.consumeSupport;
    }

    inline int HolsterLocation(const Sample& s, const Settings& c) noexcept
    {
        return s.reserveSlot == HolsterSlot::Hip ? 1 : s.reserveSlot == HolsterSlot::Shoulder ? 0 : c.holsterLocation;
    }

    // Horizontal facing of the head.
    inline bool HeadForward(Quat head, Vec& forward) noexcept
    {
        if (!Normal(head))
            return false;
        forward = Rotate(head, {0, 0, -1});
        forward.y = 0;
        const float n = Dot(forward, forward);
        if (!std::isfinite(n))
            return false;
        if (n < 0.04f)
        {
            // Looking straight down at the pouch must not destroy body yaw.
            Vec side = Rotate(head, {1, 0, 0});
            side.y = 0;
            const float length = Dot(side, side);
            if (!std::isfinite(length) || length < 0.04f)
                return false;
            side = side * (1 / std::sqrt(length));
            forward = {side.z, 0, -side.x};
        }
        else
            forward = forward * (1 / std::sqrt(n));
        return true;
    }

    // Body yaw for body-anchored zones (a VR shooter's "inventory lock"; a the earlier CE VR mod pull
    // request (34) lost its shoulder zones when the head turned far). The torso follows the
    // head only past a dead zone, and otherwise settles back under it slowly,
    // never while a hand is working a zone.
    class BodyYaw
    {
    public:
        static constexpr float kDeadZone = 0.70f; // radians, about 40 degrees
        static constexpr float kSettleSeconds = 1.5f;
        void Reset() noexcept { *this = BodyYaw{}; }
        bool Update(Quat head, uint64_t now, bool hold, Vec& forward) noexcept
        {
            Vec facing{};
            if (!now || !HeadForward(head, facing))
            {
                Reset();
                return false;
            }
            const float headYaw = std::atan2(-facing.x, -facing.z);
            if (!seeded_ || now < last_ || now - last_ > 200)
            {
                yaw_ = headYaw;
                seeded_ = true;
            }
            else
            {
                const float dt = static_cast<float>(now - last_) * 0.001f;
                const float d = Wrap(headYaw - yaw_);
                if (d > kDeadZone)
                    yaw_ += d - kDeadZone;
                else if (d < -kDeadZone)
                    yaw_ += d + kDeadZone;
                else if (!hold)
                    yaw_ += d * (1 - std::exp(-dt / kSettleSeconds));
                yaw_ = Wrap(yaw_);
            }
            last_ = now;
            forward = {-std::sin(yaw_), 0, -std::cos(yaw_)};
            return true;
        }

    private:
        static float Wrap(float a) noexcept
        {
            constexpr float pi = 3.14159265f;
            while (a > pi)
                a -= 2 * pi;
            while (a < -pi)
                a += 2 * pi;
            return a;
        }
        float yaw_{};
        uint64_t last_{};
        bool seeded_{};
    };

    // The two holster points on the gun-hand side, from the head and a horizontal
    // unit facing. Zones() uses the one the reserve weapon rides; Reach grip-held
    // uses both (GRAB-G1-PLAN.md 9.4).
    inline void HolsterPoints(Vec head, Vec forward, const Settings& c, Vec& shoulder, Vec& hip) noexcept
    {
        const Vec right{-forward.z, 0, forward.x};
        const float side = c.leftHanded ? -1.0f : 1.0f;
        hip = head + right * (side * 0.27f) + Vec{0, -Setting(c.pouchDown, 0.25f, 0.85f, 0.50f), 0};
        shoulder = head + right * (side * 0.22f) + forward * (-0.13f) + Vec{0, -0.20f, 0};
    }

    // body: optional horizontal unit facing from BodyYaw; otherwise the head's.
    inline bool Zones(const Sample& s, const Settings& c, Vec& pouch, Vec& holster, const Vec* body = nullptr) noexcept
    {
        if (!Finite(s.head) || !Finite(s.primary) || !Finite(s.support) || !Normal(s.headRotation) ||
            !Normal(s.primaryRotation))
            return false;
        Vec forward{};
        if (body && Finite(*body) && std::fabs(body->y) < 1e-3f && std::fabs(Dot(*body, *body) - 1) < 0.02f)
            forward = *body;
        else if (!HeadForward(s.headRotation, forward))
            return false;
        const Vec right{-forward.z, 0, forward.x};
        const float side = c.leftHanded ? -1.0f : 1.0f;
        const bool shoulder = c.pouchLocation == 1;
        pouch = s.head + right * (-side * (shoulder ? 0.22f : 0.25f)) + forward * (shoulder ? -0.13f : 0.06f) +
                Vec{0, shoulder ? -0.20f : -Setting(c.pouchDown, 0.25f, 0.85f, 0.50f), 0};
        pouch = pouch + right * (side * Setting(c.pouchOffset.x, -.40f, .40f, 0)) +
                Vec{0, Setting(c.pouchOffset.y, -.40f, .40f, 0), 0} +
                forward * Setting(c.pouchOffset.z, -.40f, .40f, 0);
        Vec shoulderPoint{}, hipPoint{};
        HolsterPoints(s.head, forward, c, shoulderPoint, hipPoint);
        holster = HolsterLocation(s, c) == 1 ? hipPoint : shoulderPoint;
        return true;
    }

    // One quick out-and-back translation of the weapon hand, in any direction.
    // Head-relative positions reject whole-body translation. No grip is owned.
    class SingleNeedleShake
    {
    public:
        void Reset() noexcept { *this = SingleNeedleShake{}; }
        bool Update(Vec relative, uint64_t now, float stroke) noexcept
        {
            const float travel = Setting(stroke, 0.06f, 0.20f, 0.10f);
            if (!Finite(relative) || !now)
            {
                Reset();
                return false;
            }
            if (!last_ || now <= last_ || now - last_ > 100 || travel_ != travel)
            {
                Seed(relative, now, travel);
                return false;
            }
            const uint64_t previousAt = last_;
            const Vec previous = previous_;
            const float dt = static_cast<float>(now - last_) * 0.001f;
            const Vec step = relative - previous_;
            const float distance2 = Dot(step, step);
            // Drop discontinuities instead of interpreting tracking recovery as a
            // stroke. Fast human shakes remain valid at 72, 90 and 120 Hz.
            if (!std::isfinite(distance2) || distance2 > 0.25f * 0.25f || distance2 > 64.0f * dt * dt)
            {
                Seed(relative, now, travel);
                return false;
            }
            previous_ = relative;
            last_ = now;
            const bool quiet = distance2 <= 0.20f * 0.20f * dt * dt;
            if (latched_)
            {
                if (!quiet)
                    quietAt_ = 0;
                else if (!quietAt_)
                    quietAt_ = previousAt;
                else if (now - quietAt_ >= 150)
                    Seed(relative, now, travel);
                return false;
            }
            if (!returning_)
            {
                if (quiet)
                {
                    origin_ = relative;
                    started_ = 0;
                    return false;
                }
                if (!started_)
                {
                    origin_ = previous;
                    started_ = previousAt;
                }
                const Vec outward = relative - origin_;
                const float length2 = Dot(outward, outward);
                const uint64_t elapsed = now - started_;
                if (elapsed > 300)
                {
                    Seed(relative, now, travel);
                    return false;
                }
                if (length2 >= travel * travel)
                {
                    const float length = std::sqrt(length2);
                    if (elapsed < 20 || length < 0.50f * elapsed * 0.001f)
                    {
                        Seed(relative, now, travel);
                        return false;
                    }
                    axis_ = outward * (1.0f / length);
                    peak_ = relative;
                    peakAt_ = now;
                    returning_ = true;
                }
                return false;
            }
            const float reverse = Dot(peak_ - relative, axis_);
            if (reverse < 0)
            {
                peak_ = relative;
                peakAt_ = now;
                return false;
            }
            const uint64_t elapsed = now - peakAt_;
            if (elapsed > 300)
            {
                Seed(relative, now, travel);
                return false;
            }
            if (reverse >= travel && elapsed >= 20 && reverse >= 0.50f * elapsed * 0.001f)
            {
                latched_ = true;
                quietAt_ = 0;
                return true;
            }
            return false;
        }

    private:
        void Seed(Vec p, uint64_t now, float travel) noexcept
        {
            Reset();
            previous_ = origin_ = p;
            last_ = now;
            travel_ = travel;
        }
        Vec previous_{}, origin_{}, axis_{}, peak_{};
        uint64_t last_{}, started_{}, peakAt_{}, quietAt_{};
        float travel_{};
        bool returning_{}, latched_{};
    };

    class State
    {
    public:
        void Reset() noexcept { *this = State{}; }
        void Cancel() noexcept
        {
            phase_ = 0;
            pulseUntil_ = 0;
            pulse_ = 0;
            armedP_ = armedS_ = false;
            available_ = false;
            last_ = 0;
            singleShake_.Reset();
        }
        bool HasClaimedGrip() const noexcept { return ownedP_ || ownedS_ || ownedT_; }
        static constexpr uint64_t kPendingSwapMs = 500; // first-ever load only; the layout cache covers drop-outs
        Output Update(const Sample& s, const Settings& c) noexcept
        {
            Output out{};
            const bool validP = std::isfinite(s.primaryGrip) && s.primaryGrip >= 0 && s.primaryGrip <= 1;
            const bool validS = std::isfinite(s.supportGrip) && s.supportGrip >= 0 && s.supportGrip <= 1;
            const bool heldP = validP && s.primaryGrip > 0.55f;
            const bool heldS = validS && s.supportGrip > 0.55f;
            const bool releasedP = validP && s.primaryGrip < 0.35f;
            const bool releasedS = validS && s.supportGrip < 0.35f;
            // Trigger take uses the game's own press threshold (0.15), so no grenade
            // press can leak to the game before the magazine is taken.
            // With no Reload binding the pouch cannot reload, so it must not hide
            // the support trigger either: the grenade goes through as usual.
            const bool triggerMode = c.reload && !c.takeWithGrip && c.reloadButton;
            const bool validT = std::isfinite(s.supportTrigger) && s.supportTrigger >= 0 && s.supportTrigger <= 1;
            const bool heldT = validT && s.supportTrigger > 0.15f;
            const bool releasedT = validT && s.supportTrigger < 0.10f;
            const bool identity = title_ != s.title || generation_ != s.generation || space_ != s.space ||
                                  left_ != c.leftHanded || reload_ != c.reload || holsters_ != c.holsters ||
                                  slide_ != c.holsterSlide || click_ != c.holsterClick || shake_ != c.needleShake ||
                                  (c.reload && weaponGraph_ != s.weaponGraph) || reloadButton_ != c.reloadButton ||
                                  swapButton_ != c.swapButton || location_ != HolsterLocation(s, c) ||
                                  pouchLocation_ != c.pouchLocation || pouchOffset_.x != c.pouchOffset.x ||
                                  pouchOffset_.y != c.pouchOffset.y || pouchOffset_.z != c.pouchOffset.z ||
                                  anchor_ != c.bodyAnchor || takeGrip_ != c.takeWithGrip;
            const bool gap = !last_ || s.now < last_ || s.now - last_ > 200;
            const bool ready = s.ready && s.now && s.space && s.generation && TitleIndex(s.title) >= 0 &&
                               (c.reload || c.holsters) && !s.dualWield && validP && validS;
            Vec pouch{}, holster{}, body{};
            // A new title, map or tracking space recentres the body under the head.
            if (title_ != s.title || generation_ != s.generation || space_ != s.space)
                pendingSwapAt_ = 0;
            if (!c.bodyAnchor || title_ != s.title || generation_ != s.generation || space_ != s.space)
            {
                bodyYaw_.Reset();
                reachP_ = reachS_ = -1;
                holdUntil_ = 0;
            }
            const bool anchored =
                c.bodyAnchor && bodyYaw_.Update(s.headRotation, s.now,
                                    phase_ || ownedP_ || ownedS_ || ownedT_ || s.now < holdUntil_, body);
            const bool poses = Zones(s, c, pouch, holster, anchored ? &body : nullptr);
            if (anchored && poses)
                out.bodyForward = body;
            const float holsterRadius = Setting(c.holsterRadius, 0.08f, 0.40f, 0.20f);
            if (poses && !gap)
            {
                // Freeze the body's settle briefly while a hand reaches toward a
                // zone (>0.3 m/s closing, within 0.25 m of its edge). A relaxed
                // hand resting near the pouch or hip does not pin the yaw.
                const float dt = static_cast<float>(s.now - last_) * 0.001f;
                const auto reaching = [&](Vec hand, Vec zone, float radius, float& previous)
                {
                    const Vec d = hand - zone;
                    const float distance = std::sqrt(Dot(d, d));
                    const bool closing =
                        previous >= 0 && dt > 0 && distance < radius + 0.25f && previous - distance >= 0.30f * dt;
                    previous = distance;
                    return closing;
                };
                const bool p = reaching(s.primary, holster, holsterRadius, reachP_);
                const bool q = reaching(s.support, pouch, Setting(c.zoneRadius, 0.08f, 0.40f, 0.20f), reachS_);
                if (p || q)
                    holdUntil_ = s.now + 400;
            }
            else
                reachP_ = reachS_ = -1;
            const bool resumed = ready && poses && !available_;
            if (identity || gap || !ready || !poses || resumed)
            {
                // A held grip on re-entry is not a new gesture. Keep ownership of
                // an already claimed press until release, including cancellation.
                phase_ = 0;
                pulseUntil_ = 0;
                pulse_ = 0;
                singleShake_.Reset();
                armedP_ = armedS_ = armedT_ = false;
                if (left_ != c.leftHanded)
                    std::swap(ownedP_, ownedS_);
            }
            title_ = s.title;
            generation_ = s.generation;
            space_ = s.space;
            left_ = c.leftHanded;
            reload_ = c.reload;
            holsters_ = c.holsters;
            reloadButton_ = c.reloadButton;
            swapButton_ = c.swapButton;
            location_ = HolsterLocation(s, c);
            last_ = s.now;
            pouchLocation_ = c.pouchLocation;
            pouchOffset_ = c.pouchOffset;
            anchor_ = c.bodyAnchor;
            takeGrip_ = c.takeWithGrip;
            slide_ = c.holsterSlide;
            click_ = c.holsterClick;
            shake_ = c.needleShake;
            weaponGraph_ = s.weaponGraph;
            available_ = ready && poses;
            if (releasedP)
                armedP_ = true;
            if (releasedS)
                armedS_ = true;
            if (releasedT)
                armedT_ = true;
            // Ownership includes the release frame, then ends. Ordinary grips are
            // never consumed unless the player deliberately began in a body zone.
            out.consumePrimary = ownedP_;
            out.consumeSupport = ownedS_;
            out.consumeSupportTrigger = ownedT_;
            if (releasedP)
                ownedP_ = false;
            if (releasedS)
                ownedS_ = false;
            if (releasedT)
                ownedT_ = false;
            if (!ready || !poses || identity || gap || resumed)
            {
                // Track zone presence while paused so resuming with the hand
                // already in the holster does not tick.
                inHolster_ = poses && c.holsters && Near(s.primary, holster, holsterRadius);
                holsterTick_ = {};
                holsterTick_.armed = !inHolster_; // resuming inside never ticks
                return out;
            }

            if (pendingSwapAt_)
            {
                if (s.now < pendingSwapAt_ || s.now - pendingSwapAt_ > kPendingSwapMs || !c.holsters)
                    pendingSwapAt_ = 0;
                else if (c.swapButton)
                {
                    pulse_ = c.swapButton;
                    pulseUntil_ = s.now + 120;
                    out.swapRequested = true;
                    pendingSwapAt_ = 0;
                }
            }
            const bool occupied = phase_ != 0 || ownedP_ || ownedS_ || ownedT_;
            const float pouchRadius = Setting(c.zoneRadius, 0.08f, 0.40f, 0.20f);
            // The support trigger is the magazine hand's input inside the pouch and
            // while a magazine is held; anywhere else it is ordinary game input
            // (grenade) and cancels gestures like any other action.
            const bool inPouch = Near(s.support, pouch, pouchRadius);
            const bool triggerIsReload = triggerMode && (phase_ == 1 || ownedT_ || inPouch);
            const bool otherAction = s.otherAction || (heldT && !triggerIsReload);
            // During the pouch's 1 s rest a fresh pull inside it is swallowed
            // (held until release) without a magazine, so no grenade leaks.
            if (triggerMode && inPouch && s.now < pouchCooldown_ && armedT_ && heldT && !phase_ && !ownedT_)
            {
                ownedT_ = true;
                out.consumeSupportTrigger = true;
            }
            // At the pouch the support trigger is the magazine hand's input, so even
            // a pull below the take point is hidden from the game (the raw analog
            // path with vr_action_mapping off would otherwise pass 0.10-0.15).
            if (triggerIsReload)
                out.consumeSupportTrigger = true;
            // A magazine is held until grip release; incidental trigger/button
            // input and taking time to align it must not silently drop the item.
            // Holster/shake commands retain their cancellation and timeout rules.
            if (phase_ != 1 && (otherAction || (phase_ && s.now - started_ > 4000)))
                phase_ = 0;
            if (phase_ == 1)
            {
                out.releaseTwoHand = true;
                // Trigger mode also hides the support grip while a magazine is held,
                // so a grip-bound action cannot fire mid-reload.
                out.consumeSupport = true;
                if (triggerMode)
                    out.consumeSupportTrigger = true;
                if (!Near(s.support, pouch, pouchRadius + 0.08f))
                    leftPouch_ = true;
                // Insert beside/under the firing-hand grip. Controller-local
                // offset, not an invented per-title magazine marker.
                const Vec receiver =
                    s.receiverValid ? s.receiver : s.primary + Rotate(s.primaryRotation, {0, -0.06f, -0.04f});
                const Vec magazine = s.receiverValid ? HeldMagazineCenter(s, s.supportRotation) : s.support;
                // When the magazine leaves the hand (seated or dropped) a still-held
                // support grip stays hidden until it is let go, as in grip mode.
                const bool gripStillHeld = triggerMode && validS && !releasedS;
                if (triggerMode ? releasedT : releasedS)
                {
                    phase_ = 0;
                    if (gripStillHeld)
                        ownedS_ = true;
                } // let go outside the magazine well: it vanishes
                else if (!otherAction && leftPouch_ && s.now - started_ >= 120 &&
                         (!s.receiverValid || (Finite(receiver) && Normal(s.supportRotation))) &&
                         Near(magazine, receiver, Setting(c.insertRadius, 0.06f, 0.30f, 0.13f)) &&
                         !Near(s.support, pouch, pouchRadius + 0.04f) && !Near(s.support, grabbedAt_, 0.20f))
                {
                    // Seats on ENTERING the well (snap on entry), not on release.
                    // Only the game's own reload is pressed, so its animation and
                    // timing stay in charge. The pouch then rests 1 s so a wobble
                    // back past it cannot start a second magazine.
                    pulse_ = c.reloadButton;
                    pulseUntil_ = s.now + 120;
                    cooldown_ = s.now + 500;
                    pouchCooldown_ = s.now + 1000;
                    out.reloadRequested = true;
                    out.supportHaptic = feedback::Strength(feedback::Cue::MagSeatSupport);
                    out.primaryHaptic = feedback::Strength(feedback::Cue::MagSeatWeapon);
                    phase_ = 0;
                    if (gripStillHeld)
                        ownedS_ = true;
                }
            }
            else if (phase_ == 2)
            {
                out.consumePrimary = true;
                out.releaseTwoHand = true;
                if (releasedP)
                    phase_ = 0;
                else if (!otherAction && s.now - started_ >= 120 &&
                         !Near(s.primary, holster, Setting(c.holsterRadius, 0.08f, 0.40f, 0.20f) + 0.12f) &&
                         !Near(s.primary, grabbedAt_, Setting(c.drawDistance, 0.10f, 0.50f, 0.25f)))
                {
                    cooldown_ = s.now + 500;
                    out.primaryHaptic = feedback::Strength(feedback::Cue::WeaponSwap);
                    phase_ = 0;
                    RequestSwap(c, s.now, out);
                }
            }
            else if (phase_ == 3)
            {
                out.consumePrimary = true;
                out.releaseTwoHand = true;
                if (releasedP)
                    phase_ = 0;
                else
                {
                    // Deliberate alternating vertical strokes relative to the
                    // head reject walking/translation and a single melee swing.
                    const Vec relative = s.primary - s.head;
                    const float travel = Setting(c.shakeTravel, 0.06f, 0.20f, 0.10f);
                    if (std::fabs(relative.x - shakeOrigin_.x) > 0.25f ||
                        std::fabs(relative.z - shakeOrigin_.z) > 0.25f || s.now - started_ > 1800)
                        phase_ = 0;
                    else
                    {
                        const float delta = relative.y - shakeExtreme_;
                        shakeLow_ = std::fmin(shakeLow_, relative.y);
                        shakeHigh_ = std::fmax(shakeHigh_, relative.y);
                        if ((shakeDirection_ > 0 && delta >= 0) || (shakeDirection_ < 0 && delta <= 0))
                            shakeExtreme_ = relative.y;
                        if ((shakeDirection_ == 0 && shakeHigh_ - shakeLow_ >= travel) ||
                            (shakeDirection_ > 0 && delta <= -travel) || (shakeDirection_ < 0 && delta >= travel))
                        {
                            // An implausibly rapid stroke cancels; tracking jumps
                            // cannot accumulate several reversals into a reload.
                            if (s.now - shakeAt_ < 60)
                                phase_ = 0;
                            else
                            {
                                shakeDirection_ = shakeDirection_ == 0
                                                      ? (relative.y >= (shakeHigh_ + shakeLow_) * 0.5f ? 1 : -1)
                                                      : -shakeDirection_;
                                shakeExtreme_ = relative.y;
                                shakeAt_ = s.now;
                                ++shakeStrokes_;
                                if (shakeStrokes_ >= 4)
                                {
                                    phase_ = 0;
                                    pulse_ = c.reloadButton;
                                    pulseUntil_ = s.now + 120;
                                    cooldown_ = s.now + 500;
                                    out.reloadRequested = true;
                                    out.primaryHaptic = feedback::Strength(feedback::Cue::ShakeReload);
                                }
                            }
                        }
                    }
                }
            }
            else if (!otherAction && s.now >= cooldown_)
            {
                const float radius = Setting(c.zoneRadius, 0.08f, 0.40f, 0.20f);
                // No swapButton check: on a first-ever load the game's switch
                // binding can be unknown for a moment (afterwards the shared
                // reader's layout cache bridges drop-outs), so the tap is held
                // briefly as a pending swap instead of being lost.
                if (c.holsters && (c.holsterSlide || c.holsterClick) && armedP_ && heldP &&
                    Near(s.primary, holster, Setting(c.holsterRadius, 0.08f, 0.40f, 0.20f)))
                {
                    phase_ = 2;
                    started_ = s.now;
                    grabbedAt_ = s.primary;
                    ownedP_ = true;
                    out.consumePrimary = true;
                    out.releaseTwoHand = true;
                    out.grabbedHolster = true;
                    out.primaryHaptic = feedback::Strength(feedback::Cue::HolsterGrab);
                    if (c.holsterClick)
                    {
                        // A fresh grip click in the zone exchanges once. Holding
                        // or moving cannot repeat it, even with both modes on.
                        phase_ = 0;
                        cooldown_ = s.now + 500;
                        out.primaryHaptic = feedback::Strength(feedback::Cue::WeaponSwap);
                        RequestSwap(c, s.now, out);
                    }
                }
                else if (c.reload && c.reloadButton && s.now >= pouchCooldown_ && Near(s.support, pouch, radius) &&
                         (triggerMode ? (armedT_ && heldT) : (armedS_ && heldS)))
                {
                    phase_ = 1;
                    started_ = s.now;
                    leftPouch_ = false;
                    grabbedAt_ = s.support;
                    if (triggerMode)
                    {
                        ownedT_ = true;
                        out.consumeSupportTrigger = true;
                    }
                    else
                    {
                        ownedS_ = true;
                        out.consumeSupport = true;
                    }
                    out.releaseTwoHand = true;
                    out.pickedMagazine = true;
                    out.supportHaptic = feedback::Strength(feedback::Cue::MagTake);
                }
            }
            // Pouch/holster transactions retain priority. Shake observes only the
            // weapon hand; it never consumes either grip or changes two-hand aim.
            if (c.reload && c.needleShake && c.reloadButton && NeedleWeapon(s.title, s.weaponGraph) && !otherAction &&
                !occupied && !phase_ && !out.pickedMagazine && !out.grabbedHolster)
            {
                if (singleShake_.Update(s.primary - s.head, s.now, c.shakeTravel) && s.now >= cooldown_)
                {
                    pulse_ = c.reloadButton;
                    pulseUntil_ = s.now + 120;
                    cooldown_ = s.now + 500;
                    out.reloadRequested = true;
                    out.primaryHaptic = feedback::Strength(feedback::Cue::ShakeReload);
                }
            }
            else
                singleShake_.Reset();
            // A light tick as the weapon hand enters the holster zone, so the
            // player can find it without looking (holster feedback, as in RE4 VR and
            // a 2025 PC VR shooter update). Once per entry; never while a gesture,
            // magazine or claimed grip is in progress.
            const float tickRadius = Setting(c.holsterRadius, 0.08f, 0.40f, 0.20f);
            const bool inHolster = c.holsters && Near(s.primary, holster, tickRadius);
            const float rearm = Setting(c.zoneHapticRearmM, 0.0f, 0.20f, 0.0f);
            const bool pastDeadZone = !c.holsters || !Near(s.primary, holster, tickRadius + rearm);
            const float tick = Setting(c.zoneHaptic, 0.0f, 0.50f, feedback::Strength(feedback::Cue::HolsterEnter));
            const bool entered = StepHolsterTick(holsterTick_, inHolster, pastDeadZone, s.now);
            if (tick > 0 && entered && !phase_ && !ownedP_ && !out.grabbedHolster && out.primaryHaptic <= 0)
                out.primaryHaptic = tick;
            inHolster_ = inHolster;
            if (heldP)
                armedP_ = false;
            if (heldS)
                armedS_ = false;
            if (heldT)
                armedT_ = false;
            if (s.now < pulseUntil_ && !otherAction)
            {
                out.buttons = pulse_;
                out.pulseUntil = pulseUntil_;
            }
            else
            {
                pulseUntil_ = 0;
                pulse_ = 0;
            }
            out.holdingMagazine = phase_ == 1;
            return out;
        }

    private:
        HolsterTick holsterTick_{};
        SingleNeedleShake singleShake_{};
        BodyYaw bodyYaw_{};
        uint64_t pendingSwapAt_{}; // a swap asked for before the binding was known
        void RequestSwap(const Settings& c, uint64_t now, Output& out) noexcept
        {
            if (c.swapButton)
            {
                pulse_ = c.swapButton;
                pulseUntil_ = now + 120;
                out.swapRequested = true;
                pendingSwapAt_ = 0;
            }
            else
                pendingSwapAt_ = now ? now : 1;
        }
        bool ownedT_{}, armedT_{}, takeGrip_{};
        uint64_t pouchCooldown_{};
        float reachP_{-1}, reachS_{-1};
        uint64_t holdUntil_{};
        GameTitle title_{GameTitle::None};
        uint32_t generation_{};
        uint64_t space_{}, last_{}, started_{}, cooldown_{}, pulseUntil_{};
        uint32_t pulse_{}, reloadButton_{}, swapButton_{};
        int phase_{}, location_{}, pouchLocation_{};
        Vec pouchOffset_{};
        bool left_{}, reload_{}, holsters_{}, armedP_{}, armedS_{}, ownedP_{}, ownedS_{}, leftPouch_{}, available_{};
        bool slide_{true}, click_{};
        bool shake_{};
        bool inHolster_{}, anchor_{true};
        uint64_t weaponGraph_{}, shakeAt_{};
        Vec shakeOrigin_{};
        float shakeExtreme_{}, shakeLow_{}, shakeHigh_{};
        int shakeDirection_{}, shakeStrokes_{};
        Vec grabbedAt_{};
    };

    // Snapshot transport is validated again on XInput's thread. Render suspension,
    // pause or title changes must never replay a cached reload/switch command.
    inline bool Fresh(uint64_t now, uint64_t sampled, GameTitle title, GameTitle sampledTitle, uint32_t generation,
        uint32_t sampledGeneration, bool gameplay) noexcept
    {
        return gameplay && TitleIndex(title) >= 0 && title == sampledTitle && generation &&
               generation == sampledGeneration && sampled && now >= sampled && now - sampled <= 150;
    }
} // namespace weapon_interaction
