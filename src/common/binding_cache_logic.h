#pragma once
// Cache of the last good native action bindings per controller, so a brief
// failure of a title's layout reader does not lose the bindings.
// Used by dll/core/game.cpp and tests/weapon_interaction_tests.cpp.
//
// Last-known-good native action bindings (Halo CE's layout reader drops
// out for several seconds at gameplay start while the MCC layout itself is
// unchanged). One cache per controller index, keyed by title (a title change
// clears it), NOT by generation: the MCC layout survives map loads and does
// not depend on the OpenXR profile. The epoch is bumped only by a real
// interaction-profile change event, never by session state changes. A fresh
// read always replaces the entry.
//
// Pure logic; the caller supplies the lock.
#include "vr_action_mapping.h"
#include <cstdint>

namespace binding_cache
{
    class Cache
    {
    public:
        void Store(GameTitle title, uint32_t profileEpoch, const vr_mapping::Transports& t) noexcept
        {
            valid_ = title != GameTitle::None;
            title_ = title;
            epoch_ = profileEpoch;
            transports_ = t;
        }
        bool Read(GameTitle title, uint32_t profileEpoch, vr_mapping::Transports& out) const noexcept
        {
            if (!valid_ || title != title_ || profileEpoch != epoch_)
                return false;
            out = transports_;
            return true;
        }
        bool Has(GameTitle title, uint32_t profileEpoch) const noexcept
        {
            return valid_ && title == title_ && profileEpoch == epoch_;
        }
        void Clear() noexcept { valid_ = false; }
        // A title change clears the entry, so returning to a title later never
        // revives a layout from before the switch.
        void ClearIfOtherTitle(GameTitle title) noexcept
        {
            if (valid_ && title != title_)
                valid_ = false;
        }

    private:
        vr_mapping::Transports transports_{};
        GameTitle title_{GameTitle::None};
        uint32_t epoch_{};
        bool valid_{};
    };
} // namespace binding_cache
