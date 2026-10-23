#pragma once
// Why the Reach muzzle/lens receipt builder published nothing for a frame. Each distinct
// (reason, gun) is logged once so a refusal is readable in the log instead of only
// showing up as "lens receipts empty" counters.
// Used by dll/core/game.cpp and the Reach and weapon muzzle tests. No globals, no logging.
#include <cstdint>

namespace muzzle_refusal
{
    enum class Reason : uint8_t
    {
        None = 0,
        Guard,
        NoIdentity,
        NoRow,
        OwnerCapture,
        OwnerMismatch,
        TrackingMissing,
        PaletteEmpty,
        LensNoRow,
        LensTransform,
        Count
    };
    inline constexpr const char* Name(Reason reason) noexcept
    {
        switch (reason)
        {
        case Reason::Guard:
            return "guard (config/bindings/pair scope/generation/serial)";
        case Reason::NoIdentity:
            return "render model identity unreadable or node count out of range";
        case Reason::NoRow:
            return "no muzzle row for this render model";
        case Reason::OwnerCapture:
            return "owner capture failed (unit/weapon/user storage)";
        case Reason::OwnerMismatch:
            return "owner differs from the interpolation context's unit/weapon";
        case Reason::TrackingMissing:
            return "contact tracking incomplete (serial/reference/time/wrist)";
        case Reason::PaletteEmpty:
            return "muzzle palette empty (node count or marker did not fit)";
        case Reason::LensNoRow:
            return "no kit lens node row for this gun";
        case Reason::LensTransform:
            return "lens node transform failed";
        default:
            return "none";
        }
    }
    // Bounded once-per-(reason, key) memory. Render thread only.
    class Once
    {
    public:
        bool ShouldLog(Reason reason, uint32_t key) noexcept
        {
            for (unsigned i = 0; i < used_; ++i)
                if (reason_[i] == reason && key_[i] == key)
                    return false;
            if (used_ >= kCapacity)
                return false; // table full: stay quiet rather than spam
            reason_[used_] = reason;
            key_[used_] = key;
            ++used_;
            return true;
        }

    private:
        static constexpr unsigned kCapacity = 64;
        Reason reason_[kCapacity]{};
        uint32_t key_[kCapacity]{};
        unsigned used_{};
    };
} // namespace muzzle_refusal
