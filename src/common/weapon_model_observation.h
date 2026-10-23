// Weapon model observation: when a read held-weapon model identity may be published to the rest of the mod.
// Used by src/dll/core (game.cpp, vr.cpp); tested by tests/reload_identity_tests.cpp and tests/weapon_accessory_tests.cpp.
// Pure logic: no globals, no game memory.
#pragma once
#include "titles/ce/haloce_snapshot.h"
#include "runtime_types.h"
#include <cstdint>

namespace weapon_model
{
    // The held-weapon identity is published whenever it can be read for a known
    // model: zoom kind (visor window vs lens), pistol class (two-hand visual
    // only), grab zones and holsters all read it, not only manual reload or per-gun
    // alignment (with both of those off in the config, every non-CE gun read as
    // unknown and fell back to the lens). An unfamiliar (modded) model is
    // published only when the costlier held-range classification proved it, which
    // still runs for reload/alignment only.
    inline constexpr bool PublishLegacyIdentity(bool read, bool known, bool classifiedHeld) noexcept
    {
        return read && (known || classifiedHeld);
    }

    struct Observation
    {
        GameTitle title{};
        uint32_t generation{};
        uint64_t identity{}, space{}, at{};
    };
    class Observations
    {
        halo_ce::Snapshot<Observation> titles_[6];

    public:
        bool Publish(const Observation& o) noexcept
        {
            const int i = static_cast<int>(o.title) - 1;
            return i >= 0 && i < 6 && o.generation && o.space && o.at && titles_[i].Publish(o);
        }
        uint64_t Read(GameTitle title, uint32_t generation, uint64_t space, uint64_t now) const noexcept
        {
            const int i = static_cast<int>(title) - 1;
            Observation o{};
            if (i < 0 || i >= 6 || !titles_[i].Read(o) || o.title != title || o.generation != generation ||
                o.space != space || !o.at || now < o.at || now - o.at > 150)
                return 0;
            return o.identity;
        }
    };
}
