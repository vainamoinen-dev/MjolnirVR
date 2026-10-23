#pragma once
// Grip-held weapons and holster slots (Reach, single player): drop guards, the
// stow/draw/drop request retry, the build 32 rig probe's script, and the G1b
// holster zones and hand owner. Pure logic.
// Used by dll/titles/reach/reach_grab_pickup.inl and tests/holster_logic_tests.cpp.
// Plan: docs/GRAB-G1-PLAN.md sections 7.4, 7.5, 8 and 9.
#include <cstddef>
#include <cstdint>

namespace holster
{
    // ---- Grip-held drop guards (7.5). Tested here; wired to the gun hand in G1b. ----
    struct GripSettings
    {
        float heldAt = 0.55f;        // grip at or above: Held
        float releaseAt = 0.25f;     // grip at or below: releasing
        uint64_t releaseMs = 150;    // releasing continuously this long = drop
        uint64_t trackedForMs = 200; // tracking must have been unbroken this long
        uint64_t staleMs = 50;       // an older grip sample is no evidence
    };
    struct GripInput
    {
        float grip = 0.0f;
        bool tracked = false;      // gun-hand pose position and orientation tracked
        bool actionActive = false; // the grip action is active (controller awake and on)
        uint64_t sampleAgeMs = 0;
        bool focused = false;  // session FOCUSED (headset on, no system UI)
        bool blocked = false;  // F1, MCC menu or pause, load, cinematic, or a seat
        bool gateOpen = false; // the single-player gate (GRAB-G1-PLAN.md 7.1)
        uint64_t nowMs = 0;
    };
    class GripHold
    {
    public:
        // true once when the open hand should drop the weapon.
        bool Update(const GripInput& in, const GripSettings& s = {}) noexcept
        {
            if (!in.tracked)
                lastUntracked_ = in.nowMs ? in.nowMs : 1;
            const bool guarded = !in.gateOpen || !in.tracked ||
                                 (lastUntracked_ && in.nowMs < lastUntracked_ + s.trackedForMs) || !in.actionActive ||
                                 in.sampleAgeMs > s.staleMs || !in.focused || in.blocked;
            if (guarded)
            {
                // Keep. After the guard clears, a release needs a fresh Held first.
                armed_ = false;
                releaseSince_ = 0;
                return false;
            }
            if (in.grip >= s.heldAt)
            {
                armed_ = true;
                releaseSince_ = 0;
                return false;
            }
            if (in.grip > s.releaseAt || !armed_)
            {
                releaseSince_ = 0; // the band keeps the last state and restarts the timer
                return false;
            }
            if (!releaseSince_)
                releaseSince_ = in.nowMs ? in.nowMs : 1;
            if (in.nowMs < releaseSince_ + s.releaseMs)
                return false;
            armed_ = false;
            releaseSince_ = 0;
            return true;
        }
        // A weapon just came to the hand: it must be Held once before an open hand drops it.
        void NewWeapon() noexcept
        {
            armed_ = false;
            releaseSince_ = 0;
        }
        bool Armed() const noexcept { return armed_; }

    private:
        bool armed_ = false;
        uint64_t releaseSince_ = 0, lastUntracked_ = 0;
    };

    // ---- One stow / draw / drop request, retried while the unit is busy (7.4). ----
    enum class Step
    {
        None,
        Issue,   // start (or restart) the native action now
        Landed,  // the hand role reads what was asked
        Refused, // still not there after the retry window: give up, one log line
    };
    class RoleRequest
    {
    public:
        static constexpr int kUnarmed = -1;
        void Start(int wantRole, uint64_t nowMs, uint64_t retryMs = 100, uint64_t windowMs = 500) noexcept
        {
            active_ = true;
            want_ = wantRole;
            nextIssue_ = nowMs;
            deadline_ = nowMs + windowMs;
            retryMs_ = retryMs;
        }
        Step Update(int role, uint64_t nowMs) noexcept
        {
            if (!active_)
                return Step::None;
            if (role == want_)
            {
                active_ = false;
                return Step::Landed;
            }
            if (nowMs >= deadline_)
            {
                active_ = false;
                return Step::Refused;
            }
            if (nowMs < nextIssue_)
                return Step::None;
            nextIssue_ = nowMs + retryMs_;
            return Step::Issue;
        }
        void Cancel() noexcept { active_ = false; }
        bool Active() const noexcept { return active_; }
        int Want() const noexcept { return want_; }

    private:
        bool active_ = false;
        int want_ = kUnarmed;
        uint64_t nextIssue_ = 0, deadline_ = 0, retryMs_ = 100;
    };

    // ---- Build 32 rig probe (reach_grab_probe, F1-hidden, default 0; GRAB-G1-PLAN.md 7.6, 8). ----
    enum class ProbeAction
    {
        None,
        Stow,    // put the hand weapon away (+0x34E = -1, action 0x38) and holster its slot
        Draw,    // take the holstered weapon back (+0x34E = slot, action 0x38)
        Drop,    // drop the hand weapon, hand empty (0x48F0FC, reselect false)
        ThirdOn, // third-weapon limit raised (the 2-weapon compare reads 3) for the rest of the script
        Switch,  // the switch button while the mod owns the hand (not a probe step)
    };
    struct ProbeStep
    {
        uint64_t atMs; // after the probe is ready (gate open, a weapon held 3 s, on foot)
        ProbeAction action;
    };
    // Script 1: Q1 stow, Q2 draw, then rig leg 0's drop (the floor weapon the real grab takes back).
    inline constexpr ProbeStep kScript1[]{
        {2000, ProbeAction::Stow}, {12000, ProbeAction::Draw}, {16000, ProbeAction::Drop}};
    // Script 2: Q3 / Q4 third weapon: limit raised, the hand weapon holstered; the rig grabs a floor weapon.
    inline constexpr ProbeStep kScript2[]{{2000, ProbeAction::ThirdOn}, {2000, ProbeAction::Stow}};
    // Script 3: Q5 busy refusal: stow/draw every 3 s while the rig reloads and melees.
    inline constexpr ProbeStep kScript3[]{{2000, ProbeAction::Stow}, {5000, ProbeAction::Draw},
        {8000, ProbeAction::Stow}, {11000, ProbeAction::Draw}, {14000, ProbeAction::Stow}, {17000, ProbeAction::Draw}};

    class ProbeScript
    {
    public:
        void Select(int script) noexcept
        {
            *this = ProbeScript{};
            switch (script)
            {
            case 1:
                steps_ = kScript1;
                count_ = sizeof(kScript1) / sizeof(kScript1[0]);
                break;
            case 2:
                steps_ = kScript2;
                count_ = sizeof(kScript2) / sizeof(kScript2[0]);
                break;
            case 3:
                steps_ = kScript3;
                count_ = sizeof(kScript3) / sizeof(kScript3[0]);
                break;
            default:
                break;
            }
        }
        // The next due step, once each; None when nothing is due or the script is done.
        ProbeAction Next(uint64_t msSinceReady) noexcept
        {
            if (next_ >= count_ || msSinceReady < steps_[next_].atMs)
                return ProbeAction::None;
            return steps_[next_++].action;
        }
        bool Done() const noexcept { return next_ >= count_; }
        size_t Index() const noexcept { return next_; }

    private:
        const ProbeStep* steps_ = nullptr;
        size_t count_ = 0, next_ = 0;
    };

    // ---- G1b (build 33): holster zones, what an open hand means, and the hand owner (GRAB-G1-PLAN.md 9.2-9.4). ----
    // Holster locations, numbered as weapon_interaction's holster location.
    inline constexpr int8_t kNoZone = -1, kShoulder = 0, kHip = 1;

    // Each carried slot's zone. A pinned slot (a weapon the player holstered
    // there) keeps its zone; the others take their class location, or the other
    // location when it is taken. kNoZone when both are taken or the slot is empty.
    inline void AssignZones(const uint32_t slots[4], const int8_t preferred[4], const bool pinned[4], int8_t fallback,
        int8_t out[4]) noexcept
    {
        bool taken[2]{};
        auto location = [&](int i)
        {
            const int8_t want = preferred[i] == kShoulder || preferred[i] == kHip ? preferred[i] : fallback;
            return want == kShoulder ? kShoulder : kHip;
        };
        for (int i = 0; i < 4; ++i)
        {
            out[i] = kNoZone;
            if (slots[i] != 0xFFFFFFFFu && pinned[i] && !taken[location(i)])
            {
                out[i] = location(i);
                taken[out[i]] = true;
            }
        }
        for (int i = 0; i < 4; ++i)
        {
            if (slots[i] == 0xFFFFFFFFu || out[i] != kNoZone)
                continue;
            int8_t want = location(i);
            if (taken[want])
                want = want == kHip ? kShoulder : kHip;
            if (taken[want])
                continue;
            taken[want] = true;
            out[i] = want;
        }
    }

    // The gun hand opened its grip (GripHold fired). Only a release outside every
    // zone drops (Reviewer PLAN OK 2); a zone holding another weapon keeps the gun
    // and is felt (Manager, 2026-10-22); zones that did not resolve keep it.
    enum class Release : uint8_t
    {
        Keep,     // zones unknown: nothing happens
        Drop,     // outside both zones
        Stow,     // in a free zone
        Occupied, // in a zone that holds another weapon: keep, pulse, one log line
    };
    inline Release ReleaseAt(bool zonesKnown, int8_t zone, const bool occupied[2]) noexcept
    {
        if (!zonesKnown)
            return Release::Keep;
        if (zone != kShoulder && zone != kHip)
            return Release::Drop;
        return occupied[zone] ? Release::Occupied : Release::Stow;
    }

    // An empty hand closes its grip: true once per close (Held after a release), never under a guard.
    class GripClose
    {
    public:
        bool Update(float grip, bool ok, const GripSettings& s = {}) noexcept
        {
            if (!ok)
            {
                open_ = false; // a close needs a fresh open after any guard
                return false;
            }
            if (grip <= s.releaseAt)
                open_ = true;
            if (!open_ || grip < s.heldAt)
                return false;
            open_ = false;
            return true;
        }

    private:
        bool open_ = false;
    };

    // Who owns the hand. While not Stock, the player action's desired slot
    // (+0x46) is written each tick before the game applies it, so the game's own
    // control path keeps the hand as the mod set it.
    class HandPlan
    {
    public:
        enum class Mode : uint8_t
        {
            Stock, // the mod writes nothing
            Slot,  // this slot in the hand
            Empty, // no weapon in the hand
        };
        void Take(Mode mode, int slot) noexcept
        {
            mode_ = mode;
            slot_ = mode == Mode::Slot ? slot : -1;
            seen_ = false;
        }
        void Release() noexcept { *this = HandPlan{}; }
        Mode Current() const noexcept { return mode_; }
        int Slot() const noexcept { return slot_; }
        // Each tick, own = the desired slot as the game made it. True with the
        // byte to write while the mod owns the hand. The player's own value
        // moving to something that is not an echo of the last write means the
        // switch button: back to Stock (Reviewer PLAN OK 1).
        bool Apply(int8_t own, int8_t& write) noexcept
        {
            if (mode_ == Mode::Stock)
                return false;
            if (seen_ && own != last_ && own != written_)
            {
                Release();
                return false;
            }
            const int8_t target = mode_ == Mode::Empty ? int8_t(-1) : static_cast<int8_t>(slot_);
            seen_ = true;
            last_ = own;
            written_ = write = target;
            return true;
        }
        // Carried weapons the chooser must skip: every slot but the planned hand slot.
        bool Holstered(int slot) const noexcept { return mode_ != Mode::Stock && slot != slot_; }

    private:
        Mode mode_ = Mode::Stock;
        int slot_ = -1;
        bool seen_ = false;
        int8_t last_ = 0, written_ = 0;
    };

    // A change in the carried slots between two ticks (GRAB-G1-PLAN.md 9.2;
    // Reviewer CHANGES b06bb0ff 1). While a mod request runs its own changes
    // are expected (a drop landing). Otherwise a handle that only appeared is
    // a pickup (the game's add-weapon draws it: the hand follows that slot);
    // any other change (a weapon taken, a swap) hands the hand back.
    enum class SlotChange : uint8_t
    {
        None,
        Pickup,
        Other,
    };
    inline SlotChange ClassifySlots(
        const uint32_t before[4], const uint32_t after[4], bool requested, int& pickupSlot) noexcept
    {
        pickupSlot = -1;
        if (requested)
            return SlotChange::None;
        auto carries = [](const uint32_t slots[4], uint32_t handle)
        {
            for (int i = 0; i < 4; ++i)
                if (slots[i] == handle)
                    return true;
            return false;
        };
        bool gained = false, lost = false;
        for (int i = 0; i < 4; ++i)
        {
            if (after[i] != 0xFFFFFFFFu && !carries(before, after[i]))
            {
                gained = true;
                pickupSlot = i;
            }
            if (before[i] != 0xFFFFFFFFu && !carries(after, before[i]))
                lost = true;
        }
        if (gained && !lost)
            return SlotChange::Pickup;
        if (!gained && !lost)
            return SlotChange::None;
        pickupSlot = -1;
        return SlotChange::Other;
    }

    // The game's next-weapon order (chooser 0x48ED50: slot + 1 modulo 4, the
    // first filled one), from the hand slot (-1 = empty). -1 = no other weapon.
    inline int NextCarried(const uint32_t slots[4], int role) noexcept
    {
        const int from = role >= 0 && role < 4 ? role : -1;
        for (int step = 1; step <= 4; ++step)
        {
            const int i = (from + step) & 3;
            if (i != from && slots[i] != 0xFFFFFFFFu)
                return i;
        }
        return -1;
    }

    // The hand owner's seat (build 33 rig finding 1). A sample is Unknown when
    // it is stale or the unit was not read (headset tracking lost, a frame the
    // VR camera skipped). The hand goes back to the game on a seat that held
    // kSeatConfirmMs, or on a seat unknown for kSeatUnknownMs, never on one
    // stale sample.
    enum class Seat : uint8_t
    {
        Unknown,
        OnFoot,
        Seated,
    };
    inline constexpr uint64_t kSeatConfirmMs = 200, kSeatUnknownMs = 5000;

    // Guns the player dropped with grip-held (build 33, real-life rule): each
    // stays on the floor until the hand grab takes it; the game's walk-over
    // pickup skips it. The last 4 drops. A handle the game frees and reuses
    // carries a new salt, so a stale entry never matches another object.
    class DroppedGuns
    {
    public:
        void Add(uint32_t handle) noexcept
        {
            if (handle == 0xFFFFFFFFu || Find(handle) >= 0)
                return;
            handles_[next_] = handle;
            refused_[next_] = false;
            next_ = (next_ + 1) % 4;
        }
        bool Has(uint32_t handle) const noexcept { return Find(handle) >= 0; }
        // A carried handle was taken back (the hand grab): it is free again.
        void Carried(const uint32_t slots[4]) noexcept
        {
            for (int i = 0; i < 4; ++i)
                if (const int at = Find(slots[i]); at >= 0)
                    handles_[at] = 0xFFFFFFFFu;
        }
        // True on the first refusal of a dropped handle (one log line per drop).
        bool FirstRefusal(uint32_t handle) noexcept
        {
            const int at = Find(handle);
            if (at < 0 || refused_[at])
                return false;
            refused_[at] = true;
            return true;
        }

    private:
        int Find(uint32_t handle) const noexcept
        {
            if (handle == 0xFFFFFFFFu)
                return -1;
            for (int i = 0; i < 4; ++i)
                if (handles_[i] == handle)
                    return i;
            return -1;
        }
        uint32_t handles_[4]{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
        bool refused_[4]{};
        int next_ = 0;
    };
    class SeatConfirm
    {
    public:
        // OnFoot at once; Seated once it held kSeatConfirmMs; otherwise Unknown.
        Seat Update(Seat sample, uint64_t nowMs) noexcept
        {
            if (sample != Seat::Seated)
                seated_ = false;
            else if (!seated_)
            {
                seated_ = true;
                seatedAt_ = nowMs;
            }
            if (sample != Seat::Unknown)
                unknown_ = false;
            else if (!unknown_)
            {
                unknown_ = true;
                unknownAt_ = nowMs;
            }
            if (sample == Seat::Seated)
                return nowMs - seatedAt_ >= kSeatConfirmMs ? Seat::Seated : Seat::Unknown;
            return sample;
        }
        bool LongUnknown(uint64_t nowMs) const noexcept { return unknown_ && nowMs - unknownAt_ >= kSeatUnknownMs; }

    private:
        bool seated_ = false, unknown_ = false;
        uint64_t seatedAt_ = 0, unknownAt_ = 0;
    };

} // namespace holster
