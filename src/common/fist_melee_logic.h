#pragma once
// Fist punch melee rules: when an empty hand counts as a fist that may strike.
// Used by dll/core/contact_melee_queue.h, dll/core/vr.cpp and tests/fist_melee_tests.cpp.
//
// Fist punch melee: with `melee_fist` on, a bare hand only
// strikes through the existing contact melee (physical_melee) while it is a
// fist. A fist is the grip squeezed in open air on an empty hand (Quest grips
// curl the lower fingers); the trigger only curls the index finger visually,
// so the off-hand trigger stays the grenade. A grip that a body slot,
// holster, magazine, two-hand support or second weapon claims is never a fist,
// and stays not-a-fist until it is let go. Weapon points (the held gun) are
// never gated: gun bashes work as before.
//
// Pure logic: no globals.
#include <cmath>

namespace fist_melee
{
    inline constexpr float kFistGrip = 0.65f, kOpenGrip = 0.30f;

    struct HandState
    {
        bool tracked{};
        float grip{};
        bool claimed{}; // the grip belongs to a slot/holster/magazine/support grip/second weapon
    };

    class Latch
    {
    public:
        void Reset() noexcept { fist_ = spoiled_ = false; }
        bool Fist() const noexcept { return fist_; }
        bool Update(const HandState& h) noexcept
        {
            const bool valid = h.tracked && std::isfinite(h.grip);
            const bool open = !valid || h.grip <= kOpenGrip;
            if (open)
            {
                Reset();
                return false;
            }
            if (h.claimed)
                spoiled_ = true;
            if (spoiled_)
            {
                fist_ = false;
                return false;
            }
            if (h.grip >= kFistGrip)
                fist_ = true;
            return fist_;
        }

    private:
        bool fist_{}, spoiled_{};
    };

    // The support hand holds nothing while the weapon hand carries the gun, which strikes with no
    // squeeze. With support_free the empty support hand strikes the same way (no squeeze) unless a
    // grip gesture owns it (holster, magazine, two-hand grab, second weapon); a squeezed fist still
    // strikes either way.
    inline bool SupportStrikes(bool fist, bool supportFree, bool tracked, bool claimed) noexcept
    {
        return tracked && !claimed && (fist || supportFree);
    }

    // How many leading contact points (the bare hand's nodes) must come from a
    // fist to strike. 0 leaves the frame ungated, as before.
    inline unsigned GatedPoints(bool enabled, unsigned handNodes, bool holdsWeapon) noexcept
    {
        return enabled && !holdsWeapon ? handNodes : 0;
    }
} // namespace fist_melee
