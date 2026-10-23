#pragma once
// Off-hand body slots: chest grenade throw, grenade-type switch and equipment slot.
// Used by dll/core/vr.cpp and tests/grenade_throw_tests.cpp.
//
// Off-hand body slots (the universal Immersive layout). All are body-anchored
// like the holsters and never claim a hand that is in the magazine pouch.
//
// - Chest grenade slot (support TRIGGER): pull to take a grenade; let go while
//   the hand moves fast to throw it with Halo's own grenade action (the engine
//   still aims it); let go slowly and it goes back.
// - Back-hip grenade-type slot (support GRIP): a grab switches grenade type
//   with Halo's own switch-grenade action.
// - Front-belt equipment slot (support GRIP, Halo 3): grab equipment, then
//   toss or drop it anywhere outside the slot to deploy it (Halo's own
//   equipment action); let go inside the slot to put it back.
//
// The hip slots follow the pouch depth setting. Zones that share a hand and
// an input never overlap at usual pouch depths (0.45-0.85 m); at shallower
// settings the pouch keeps priority. Weapon-hand holsters cannot conflict.
//
// Pure logic: no globals, no OpenXR, no logging.
#include "weapon_interaction_logic.h"
#include <cmath>
#include <cstdint>

namespace grenade_throw
{
    using weapon_interaction::Vec;
    using weapon_interaction::Near;
    using weapon_interaction::Setting;

    struct Settings
    {
        bool enabled{};       // chest grenade throw
        bool typeSlot{};      // back-hip grenade-type switch
        bool equipmentSlot{}; // front-belt equipment (title must have equipment)
        bool leftHanded{};
        float pouchDown{0.50f};  // weapon_pouch_down_m: hip depth below the head
        float slotRadius{0.12f}; // chest grenade slot
        float slotDown{0.25f};   // chest slot below the head
        float slotForward{0.18f};
        float typeRadius{0.10f}, equipmentRadius{0.10f};
        float throwSpeed{1.8f}; // m/s, 5-frame average at release
        float haptic{0.25f};    // overall strength, 0 = off
        uint32_t throwButton{}, cycleButton{}, equipmentButton{};
    };

    struct Input
    {
        uint64_t now{};
        bool ready{}; // gameplay, tracked, not dual wielding
        Vec head{}, support{};
        Vec bodyForward{};  // horizontal unit facing (weapon_interaction output or head)
        float trigger{};    // support trigger 0..1, NaN = unavailable
        float grip{};       // support grip 0..1, NaN = unavailable
        bool pouchActive{}; // magazine pouch exists (manual reload)
        Vec pouch{};
        float pouchRadius{};
        bool reloadOwnsTrigger{}; // the magazine gesture already owns the trigger
        bool supportBusy{};       // magazine held, barrel gripped, or grip owned elsewhere
    };

    struct Output
    {
        bool consumeTrigger{}, consumeGrip{};
        bool took{}, thrown{}, returned{}, cycled{}, deployed{};
        bool holding{}, holdingEquipment{};
        float haptic{};
        uint32_t buttons{};
        uint64_t pulseUntil{};
    };

    struct SlotPositions
    {
        Vec chest{}, typeSlot{}, equipment{};
    };

    inline bool Slots(const Input& in, const Settings& c, SlotPositions& out) noexcept
    {
        const Vec f = in.bodyForward;
        if (!weapon_interaction::Finite(in.head) || !weapon_interaction::Finite(f) ||
            std::fabs(weapon_interaction::Dot(f, f) - 1.0f) > 0.02f || std::fabs(f.y) > 1e-3f)
            return false;
        const Vec right{-f.z, 0, f.x};
        const float support = c.leftHanded ? 1.0f : -1.0f; // toward the support side
        const float hip = Setting(c.pouchDown, 0.25f, 0.85f, 0.50f);
        out.chest = in.head + f * Setting(c.slotForward, 0.0f, 0.40f, 0.18f) +
                    Vec{0, -Setting(c.slotDown, 0.10f, 0.60f, 0.25f), 0};
        // Support side, behind the hip: clear of the front-hip magazine pouch.
        out.typeSlot = in.head + right * (support * 0.25f) + f * (-0.32f) + Vec{0, -hip, 0};
        // Front of the belt, centred, a little below the hips.
        out.equipment = in.head + f * 0.18f + Vec{0, -(hip + 0.15f), 0};
        return true;
    }
    // Kept for callers/tests that only need the chest slot.
    inline bool SlotPosition(const Input& in, const Settings& c, Vec& slot) noexcept
    {
        SlotPositions s{};
        if (!Slots(in, c, s))
            return false;
        slot = s.chest;
        return true;
    }

    // Average hand speed over the last five frame intervals. A tracking gap or a
    // jump resets it, so recovered tracking can never read as a throw.
    class Speed
    {
    public:
        void Reset() noexcept { count_ = 0; }
        void Push(uint64_t now, Vec p) noexcept
        {
            if (!weapon_interaction::Finite(p) || !now)
            {
                Reset();
                return;
            }
            if (count_)
            {
                const Sample& last = samples_[(head_ + kSize - 1) % kSize];
                if (now <= last.t || now - last.t > 100)
                    Reset();
            }
            samples_[head_] = {now, p};
            head_ = (head_ + 1) % kSize;
            if (count_ < kSize)
                ++count_;
        }
        float MetresPerSecond() const noexcept
        {
            if (count_ < 2)
                return 0.0f;
            const Sample& newest = samples_[(head_ + kSize - 1) % kSize];
            const Sample& oldest = samples_[(head_ + kSize - count_) % kSize];
            const float dt = static_cast<float>(newest.t - oldest.t) * 0.001f;
            const Vec d = newest.p - oldest.p;
            const float v = dt > 0 ? std::sqrt(weapon_interaction::Dot(d, d)) / dt : 0.0f;
            return std::isfinite(v) ? v : 0.0f;
        }

    private:
        struct Sample
        {
            uint64_t t{};
            Vec p{};
        };
        static constexpr unsigned kSize = 6; // five intervals
        Sample samples_[kSize]{};
        unsigned head_{}, count_{};
    };

    class State
    {
    public:
        void Reset() noexcept { *this = State{}; }
        bool OwnsTrigger() const noexcept { return ownedT_; }
        bool OwnsGrip() const noexcept { return ownedG_; }
        Output Update(const Input& in, const Settings& c) noexcept
        {
            Output out{};
            const bool validT = std::isfinite(in.trigger) && in.trigger >= 0 && in.trigger <= 1;
            const bool heldT = validT && in.trigger > 0.15f; // the game's own press point
            const bool releasedT = validT && in.trigger < 0.10f;
            const bool validG = std::isfinite(in.grip) && in.grip >= 0 && in.grip <= 1;
            const bool heldG = validG && in.grip > 0.55f;
            const bool releasedG = validG && in.grip < 0.35f;
            if (releasedT)
                armedT_ = true;
            if (releasedG)
                armedG_ = true;
            speed_.Push(in.ready ? in.now : 0, in.support);
            SlotPositions slots{};
            const bool any = c.enabled || c.typeSlot || c.equipmentSlot;
            const bool live = any && in.ready && Slots(in, c, slots);
            const float strength = Setting(c.haptic, 0.0f, 1.0f, 0.25f) / 0.25f;
            if (!live)
            {
                // Never act from a cancelled hold; keep the inputs hidden until let
                // go so nothing fires on resume.
                grenade_ = equipment_ = false;
                out.consumeTrigger = ownedT_;
                out.consumeGrip = ownedG_;
                if (releasedT)
                    ownedT_ = false;
                if (releasedG)
                    ownedG_ = false;
                if (!any)
                    armedT_ = armedG_ = false;
                return Finish(out, in);
            }
            const float radius = Setting(c.slotRadius, 0.06f, 0.25f, 0.12f);
            const float typeRadius = Setting(c.typeRadius, 0.06f, 0.20f, 0.10f);
            const float equipmentRadius = Setting(c.equipmentRadius, 0.06f, 0.20f, 0.10f);
            const bool inPouch = in.pouchActive && Near(in.support, in.pouch, in.pouchRadius);
            const bool inChest = c.enabled && !inPouch && Near(in.support, slots.chest, radius);
            const bool inType = c.typeSlot && !inPouch && Near(in.support, slots.typeSlot, typeRadius);
            const bool inEquipment =
                c.equipmentSlot && c.equipmentButton && !inPouch && Near(in.support, slots.equipment, equipmentRadius);

            // Chest grenade (trigger).
            if (grenade_)
            {
                out.consumeTrigger = true;
                out.holding = true;
                if (releasedT)
                {
                    grenade_ = false;
                    out.holding = false;
                    const bool leftSlot = !Near(in.support, slots.chest, radius + 0.05f);
                    if (leftSlot && speed_.MetresPerSecond() >= Setting(c.throwSpeed, 1.0f, 4.0f, 1.8f) &&
                        c.throwButton)
                    {
                        Pulse(c.throwButton, in.now);
                        out.thrown = true;
                        out.haptic = feedback::Strength(feedback::Cue::GrenadeThrow) * strength;
                    }
                    else
                        out.returned = true; // a slow let-go puts it back: no throw
                    cooldown_ = in.now + 300;
                }
            }
            else if (!ownedT_ && !in.reloadOwnsTrigger && !in.supportBusy && armedT_ && heldT && inChest &&
                     in.now >= cooldown_)
            {
                grenade_ = ownedT_ = true;
                out.took = out.holding = out.consumeTrigger = true;
                out.haptic = feedback::Strength(feedback::Cue::GrenadeTake) * strength;
            }

            // Equipment (grip): take at the belt, deploy by letting go outside it.
            if (equipment_)
            {
                out.consumeGrip = true;
                out.holdingEquipment = true;
                if (releasedG)
                {
                    equipment_ = false;
                    out.holdingEquipment = false;
                    if (!Near(in.support, slots.equipment, equipmentRadius + 0.05f))
                    {
                        Pulse(c.equipmentButton, in.now);
                        out.deployed = true;
                        out.haptic =
                            std::fmax(out.haptic, feedback::Strength(feedback::Cue::EquipmentDeploy) * strength);
                    }
                    else
                        out.returned = true;
                    cooldown_ = in.now + 300;
                }
            }
            else if (!ownedG_ && !in.supportBusy && armedG_ && heldG && in.now >= cooldown_)
            {
                if (inEquipment)
                {
                    equipment_ = ownedG_ = true;
                    out.consumeGrip = true;
                    out.holdingEquipment = true;
                    out.haptic = std::fmax(out.haptic, feedback::Strength(feedback::Cue::EquipmentTake) * strength);
                }
                else if (inType && c.cycleButton)
                {
                    // A grab at the back hip switches grenade type once; the grip
                    // stays hidden until it is let go.
                    ownedG_ = true;
                    out.consumeGrip = true;
                    Pulse(c.cycleButton, in.now);
                    out.cycled = true;
                    out.haptic = std::fmax(out.haptic, feedback::Strength(feedback::Cue::GrenadeSwitch) * strength);
                    cooldown_ = in.now + 300;
                }
            }

            // In the chest slot even a light pull is the slot's, never a stray grenade.
            if (inChest && !in.reloadOwnsTrigger && !in.supportBusy)
                out.consumeTrigger = true;
            if (ownedT_)
                out.consumeTrigger = true;
            if (ownedG_)
                out.consumeGrip = true;
            if (releasedT)
                ownedT_ = false;
            if (releasedG)
                ownedG_ = false;
            if (heldT)
                armedT_ = false;
            if (heldG)
                armedG_ = false;
            return Finish(out, in);
        }

    private:
        void Pulse(uint32_t button, uint64_t now) noexcept
        {
            pulse_ = button;
            pulseUntil_ = now + 120;
        }
        Output& Finish(Output& out, const Input& in) noexcept
        {
            if (in.now < pulseUntil_)
            {
                out.buttons = pulse_;
                out.pulseUntil = pulseUntil_;
            }
            else
            {
                pulse_ = 0;
                pulseUntil_ = 0;
            }
            if (!std::isfinite(out.haptic) || out.haptic < 0)
                out.haptic = 0;
            if (out.haptic > 1)
                out.haptic = 1;
            return out;
        }
        Speed speed_{};
        uint64_t cooldown_{}, pulseUntil_{};
        uint32_t pulse_{};
        bool grenade_{}, equipment_{}, ownedT_{}, ownedG_{}, armedT_{}, armedG_{};
    };
} // namespace grenade_throw
