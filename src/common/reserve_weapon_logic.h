#pragma once
// Reserve (stowed) weapon identity for holsters, without new game reads.
// Used by weapon_inventory_logic.h and src/dll/core/game.cpp; tested by tests/reserve_weapon_tests.cpp
// and the Reach/ODST muzzle tests. It never reads game memory itself: callers pass what they observed.
//
// The held weapon's catalog identity is already observed from its first-person
// render model, and each title's proven inventory reader gives the unit's four
// weapon handles and role bytes. The reserve weapon has no first-person model,
// so its identity is LEARNED: while a weapon handle is the primary and the same
// held identity has been observed for kStableMs, that handle -> identity pair
// is remembered. When the handle later sits in the reserve, its remembered
// identity is the answer. A reserve never drawn yet answers 0 (Unknown), so
// holsters keep their configured location.
//
// kStableMs outlasts a weapon swap: the old first-person model can still be
// drawn briefly after the role byte has moved to the new handle, and that
// transient pairing must never be learned.
//
// Pure logic: no globals, no locks, no logging. One owner thread.
#include <cstdint>

namespace reserve_weapon
{
    inline constexpr uint32_t kNoHandle = UINT32_MAX;
    inline constexpr uint64_t kStableMs = 1500;
    // The candidate's identity must keep being drawn: a gap longer than this
    // restarts the window. An old model drawn once at a swap therefore cannot be
    // learned for the new handle (even when the new weapon's own model is not in
    // the catalog and so never displaces the stale candidate).
    inline constexpr uint64_t kMaxGapMs = 250;
    inline constexpr int kInventorySlots = 4;

    // The single owned weapon in the inventory that holds neither role. Ambiguity
    // (two candidates) or an invalid primary role answers kNoHandle.
    template <class OwnedWeapon>
    uint32_t SelectReserve(const uint32_t inventory[kInventorySlots], uint8_t primaryRole, uint8_t secondaryRole,
        OwnedWeapon owned) noexcept
    {
        if (primaryRole >= kInventorySlots)
            return kNoHandle;
        uint32_t reserve = kNoHandle;
        for (int i = 0; i < kInventorySlots; ++i)
        {
            if (i == primaryRole || i == secondaryRole)
                continue;
            const uint32_t handle = inventory[i];
            if (handle == kNoHandle || !(handle >> 16) || handle == inventory[primaryRole])
                continue;
            if (!owned(handle))
                continue;
            if (reserve != kNoHandle && reserve != handle)
                return kNoHandle;
            reserve = handle;
        }
        return reserve;
    }

    class Tracker
    {
    public:
        // primary: handle in the primary role (kNoHandle if none); held: the held
        // first-person identity observed now (0 if unknown).
        //
        // An unknown identity is NO evidence, not a gap: Reach builds the
        // first-person ARMS and the weapon through the same observed palette call,
        // so a non-catalog model (identity 0) arrives between weapon observations
        // every frame. Treating it as a break restarted the stable window forever
        // and nothing was ever learned (the log read "Holster reserve: Reach
        // class=Unknown slot=default"). Only a different primary or a different
        // known identity restarts the window; a held weapon whose model is not in
        // the catalog therefore never pairs a stale identity with a new handle,
        // because learning needs that identity drawn continuously (no gap longer
        // than kMaxGapMs) for kStableMs.
        void Observe(uint32_t primary, uint64_t held, uint64_t nowMs) noexcept
        {
            if (primary == kNoHandle)
            {
                m_candidate = {};
                return;
            }
            if (!held)
                return;
            if (m_candidate.handle != primary || m_candidate.identity != held || nowMs < m_since ||
                nowMs < m_lastSeen || nowMs - m_lastSeen > kMaxGapMs)
            {
                m_candidate = {primary, held};
                m_since = m_lastSeen = nowMs;
                return;
            }
            m_lastSeen = nowMs;
            if (nowMs - m_since >= kStableMs)
                Learn(primary, held);
        }

        uint64_t IdentityOf(uint32_t handle) const noexcept
        {
            if (handle == kNoHandle)
                return 0;
            for (const Entry& e : m_learned)
                if (e.handle == handle)
                    return e.identity;
            return 0;
        }

        void Reset() noexcept { *this = Tracker{}; }
        // Diagnostics for test logs: the pairing being timed and when it started.
        uint32_t CandidateHandle() const noexcept { return m_candidate.handle; }
        uint64_t CandidateIdentity() const noexcept { return m_candidate.identity; }
        uint64_t CandidateSince() const noexcept { return m_since; }

    private:
        struct Entry
        {
            uint32_t handle = kNoHandle;
            uint64_t identity = 0;
        };
        void Learn(uint32_t handle, uint64_t identity) noexcept
        {
            for (Entry& e : m_learned)
                if (e.handle == handle)
                {
                    e.identity = identity;
                    return;
                }
            for (int i = kInventorySlots - 1; i > 0; --i)
                m_learned[i] = m_learned[i - 1];
            m_learned[0] = {handle, identity}; // newest first; oldest falls off
        }
        Entry m_learned[kInventorySlots]{};
        Entry m_candidate{};
        uint64_t m_since = 0;
        uint64_t m_lastSeen = 0; // last time the candidate identity was drawn
    };

    // For titles without a proven unit-inventory reader (Halo 2, Halo CE): the
    // reserve is the weapon held before the current one, while the unit still owns
    // it and holds it in neither hand. A swap makes the old weapon the reserve; a
    // pickup that drops the old weapon fails the ownership check (Unknown).
    class PreviousHeld
    {
    public:
        void Observe(uint32_t held) noexcept
        {
            if (held == kNoHandle || held == m_current)
                return;
            m_previous = m_current;
            m_current = held;
        }
        // owned(handle) is the title's proven "this weapon belongs to the unit".
        template <class Owned>
        uint32_t Reserve(uint32_t held0, uint32_t held1, Owned owned) const noexcept
        {
            const uint32_t candidate = m_previous;
            if (candidate == kNoHandle || !(candidate >> 16) || candidate == held0 || candidate == held1 ||
                !owned(candidate))
                return kNoHandle;
            return candidate;
        }
        void Reset() noexcept { *this = PreviousHeld{}; }
        uint32_t Previous() const noexcept { return m_previous; } // diagnostics

    private:
        uint32_t m_current = kNoHandle, m_previous = kNoHandle;
    };
} // namespace reserve_weapon
